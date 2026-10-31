; RENAME.COM (hw/oodos.md, "The utilities, phase 4b"; the User's Guide's
; RENAME, 5-81 to 5-83): RENAME {new-filespec=old-filespec}.
;
;   no wildcard the built-in's: the drive old's if given, else new's (both
;               given: the same); old not there: No File; new there
;               already: "Not renamed: NEW.TYP file already exists, delete
;               (Y/N)?", a Y deleting it (new's password) and renaming;
;               then 23, old's password in the DMA buffer
;   wildcards   new's '?'s (152 fills out a '*') where old's are, else
;               "Invalid wildcard."; each matching file of the current user
;               (the names read first) renamed to new with old's characters
;               in the '?' places, each asked about as above when the new
;               name is there already, and each done shown, NEW.TYP=OLD.TYP
;   no tail     "Enter New Name: " and "Enter Old Name: ", a line each (an
;               empty one, or one with no file specification, ends it),
;               then as above, each done shown
;
; Errors: "Incorrect file specification." for a form that isn't new=old
; (an option list, a blank name, two drives, more or fewer names: ours), a
; specification 152 refuses, and new the same file as old once the drive
; is decided (ours, as the built-in refuses it: a Y to delete new would
; delete old); No File; the BDOS's said (error mode $FE). The return code:
; $FF00 after an error, No File, or a file not renamed.

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        stz     shown
        jsr     UT_OPTS
        bne     incorrect               ; RENAME takes no options (ours)
        lda     #<new
        ldx     #>new
        jsr     UT_SPEC
        bcc     @new
        cmp     #0
        bne     incorrect
        jmp     ask                     ; no tail: asked for
@new:   lda     UT_SDELIM
        cmp     #'='
        bne     incorrect
        lda     #<old
        ldx     #>old
        jsr     UT_SPEC
        bcs     incorrect
        jsr     nomore
        jmp     names

incorrect:
        lda     #<sinc
        ldx     #>sinc
        ; falls through
; The message at A/X, the return code $FF00, the end.
failsay:
        jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT

; No more specifications in the tail; else incorrect.
nomore: lda     #<wfcb
        ldx     #>wfcb
        jsr     UT_SPEC
        bcc     incorrect
        cmp     #0
        bne     incorrect
        rts

; No tail: the two names asked for, each shown when done.
ask:    lda     #1
        sta     shown
        lda     #<snew
        ldx     #>snew
        jsr     askone
        ldx     #35
:       lda     got,x
        sta     new,x
        dex
        bpl     :-
        lda     #<sold
        ldx     #>sold
        jsr     askone
        ldx     #35
:       lda     got,x
        sta     old,x
        dex
        bpl     :-
        ; falls through
; new and old: the drive, the forms, then the files.
names:  lda     new                     ; the drive: new's, or old's; both the same
        beq     @old
        ldx     old
        beq     @drive
        cmp     old
        beq     @drive
        bra     incorrect
@old:   lda     old
@drive: sta     drive
        lda     new + 1                 ; neither blank
        cmp     #' '
        beq     incorrect
        lda     old + 1
        cmp     #' '
        beq     incorrect
        ldx     #10                     ; new the same as old: refused
@same:  lda     new + 1,x
        cmp     old + 1,x
        bne     @differ
        dex
        bpl     @same
        bra     incorrect
@differ:
        lda     #<(new + 1)
        ldx     #>(new + 1)
        jsr     UT_ISWILD
        bcs     wild
        lda     #<(old + 1)
        ldx     #>(old + 1)
        jsr     UT_ISWILD
        bcs     badwild
        ldx     #10                     ; one file: old there?
:       lda     old + 1,x
        sta     oname,x
        lda     new + 1,x
        sta     nname,x
        dex
        bpl     :-
        lda     #<oname
        ldx     #>oname
        jsr     search
        bcc     @there
        cmp     #0
        bne     @end                    ; said
        jsr     nofile
@end:   jmp     UT_EXIT
@there: jsr     one
        jmp     UT_EXIT

badwild:
        lda     #<swild
        ldx     #>swild
        jmp     failsay

; Wildcards: the '?'s in the same places; the names first, then each.
wild:   ldx     #10
@where: lda     new + 1,x
        cmp     #'?'
        beq     @q
        lda     old + 1,x
        cmp     #'?'
        beq     badwild
        bra     @on
@q:     lda     old + 1,x
        cmp     #'?'
        bne     badwild
@on:    dex
        bpl     @where
        lda     #1
        sta     shown
        lda     drive
        sta     wfcb
        ldx     #10
:       lda     old + 1,x
        sta     wfcb + 1,x
        dex
        bpl     :-
        lda     #<wfcb
        ldx     #>wfcb
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
        bne     @next
        cpx     #0
        bne     @next
        jsr     nofile
        jmp     UT_EXIT
@next:  jsr     UT_NNEXT
        bcc     :+
        jmp     UT_EXIT
:       sta     @src + 1                ; oname the file's, nname new's with
        stx     @src + 2                ; its characters in the '?' places
        sta     @src2 + 1
        stx     @src2 + 2
        ldx     #10
@src:   lda     $FFFF,x
        sta     oname,x
        lda     new + 1,x
        cmp     #'?'
        bne     :+
@src2:  lda     $FFFF,x
:       sta     nname,x
        dex
        bpl     @src
        jsr     one
        bra     @next

; Rename oname to nname on the drive: new there already asked about first.
; Shown when done, if shown is set.
one:    lda     #<nname
        ldx     #>nname
        jsr     search
        bcc     @exists
        cmp     #0
        beq     @rename
        rts                             ; said
@exists:
        lda     #<snotren
        ldx     #>snotren
        jsr     CM_PUTS
        lda     #<nname
        ldx     #>nname
        jsr     CM_FNAME
        lda     #<sexists
        ldx     #>sexists
        jsr     CM_PUTS
        jsr     CM_ASK
        bcs     @delete
        jmp     UT_FAIL                 ; not renamed
@delete:
        lda     #<nname                 ; new deleted, with its password
        ldx     #>nname
        jsr     setw
        lda     #<(new + 16)
        ldx     #>(new + 16)
        jsr     UT_PWSET
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_DELETE
        jsr     BDOS
        cmp     #$FF
        bne     @rename
        jmp     UT_SAID
@rename:
        lda     #<oname                 ; wfcb: old, new at 16; old's password
        ldx     #>oname
        jsr     setw
        ldx     #10
:       lda     nname,x
        sta     wfcb + 17,x
        dex
        bpl     :-
        lda     #<(old + 16)
        ldx     #>(old + 16)
        jsr     UT_PWSET
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_RENAME
        jsr     BDOS
        cmp     #$FF
        bne     @done
        cpx     #0
        bne     :+
        jmp     nofile
:       jmp     UT_SAID
@done:  lda     shown
        beq     @quiet
        lda     #<nname                 ; NEW.TYP=OLD.TYP
        ldx     #>nname
        jsr     CM_FNAME
        lda     #'='
        jsr     CM_PUTC
        lda     #<oname
        ldx     #>oname
        jsr     CM_FNAME
        jsr     CM_EOL
@quiet: rts

; wfcb: the drive, the 11 bytes at A/X, the rest 0.
setw:   sta     @src + 1
        stx     @src + 2
        lda     drive
        sta     wfcb
        ldx     #10
@src:   lda     $FFFF,x
        sta     wfcb + 1,x
        dex
        bpl     @src
        ldx     #35 - 12
        lda     #0
:       sta     wfcb + 12,x
        dex
        bpl     :-
        rts

; Search (17) for the 11 bytes at A/X on the drive: carry clear there;
; carry set and A 0 not there, $FF an error the BDOS said (UT_SAID's).
search: jsr     setw
        lda     #<UT_REC
        ldx     #>UT_REC
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_SFIRST
        jsr     BDOS
        cmp     #$FF
        bne     @there
        cpx     #0
        bne     @said
        lda     #0
        sec
        rts
@said:  jsr     UT_SAID
        lda     #$FF
        sec
        rts
@there: clc
        rts

; A name asked for: the prompt at A/X, the line's specification to got. An
; empty line, or none in it, ends RENAME; anything but one specification
; is incorrect.
askone: jsr     UT_READ
        beq     @end
        lda     #<got
        ldx     #>got
        jsr     UT_SPEC
        bcc     @one
        cmp     #0
        bne     @bad
@end:   jmp     UT_EXIT
@one:   lda     UT_SDELIM
        bne     @bad
        jmp     nomore
@bad:   jmp     incorrect

nofile: jsr     UT_FAIL
        lda     #<snofile
        ldx     #>snofile
        jmp     UT_SAY

        .include "cmd.inc"
        .include "util.inc"

        .rodata
snew:   .byte   "Enter New Name: $"
sold:   .byte   "Enter Old Name: $"
snotren: .byte  "Not renamed: $"
sexists: .byte  " file already exists, delete (Y/N)?$"
sinc:   .byte   "Incorrect file specification.$"
swild:  .byte   "Invalid wildcard.$"
snofile: .byte  "No File$"
smem:   .byte   "Not Enough Memory$"

        .bss
shown:  .res    1                       ; 1: each rename shown
drive:  .res    1                       ; the FCB drive: 0 the current, 1-16
got:    .res    36                      ; a name asked for
new:    .res    36                      ; the specifications
old:    .res    36
wfcb:   .res    36                      ; searched, deleted, renamed
oname:  .res    11                      ; the file renamed, and its new name
nname:  .res    11

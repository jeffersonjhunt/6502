; ERASE.COM (hw/oodos.md, "The utilities, phase 4b"; the User's Guide's
; ERASE, 5-38): ERASE {filespec} {[CONFIRM]}, CONFIRM or any prefix (C).
;
;   no CONFIRM  the built-in's: a single name deleted (19, its password in
;               the DMA buffer), a wildcard asked about first
;               ("ERASE M:*.TXT (Y/N)?", the specification as typed, its
;               password not shown), No File, the BDOS's errors
;   CONFIRM     the matching files of the current user read first (17,
;               18), then each asked about, "ERASE M:ONE.TXT (Y/N)? ", and
;               deleted on a Y; a single name too
;   none        "Enter filename: " and a line read, taken as the tail; an
;               empty line, or one with no file specification, ends it
;
; Errors: "Missing delimiter or Unrecognized Option" for any other option,
; a value, a second list or one not closed (ours), or a second name;
; "Invalid file specification." for one 152 refuses; the BDOS's said
; (error mode $FE). The return code: $FF00 after an error or No File, 0
; when a question kept the files.

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        stz     confirm
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
@have:  lda     confirm
        bne     each
        lda     #<(fcb + 1)
        ldx     #>(fcb + 1)
        jsr     UT_ISWILD
        bcc     @erase
        lda     #<serase                ; a wildcard: asked about, as typed
        ldx     #>serase
        jsr     CM_PUTS
        jsr     UT_PUTSPEC
        lda     #<syn
        ldx     #>syn
        jsr     CM_PUTS
        jsr     CM_ASK
        bcc     @done                   ; kept, as asked
@erase: lda     #<(fcb + 1)
        ldx     #>(fcb + 1)
        jsr     delete
        bcc     @done
        cmp     #0
        bne     @done                   ; said
        jsr     nofile
@done:  jmp     UT_EXIT

; CONFIRM: the names first, then each asked about.
each:   lda     #<fcb
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
        bne     @next
        cpx     #0
        bne     @next
        jsr     nofile
        jmp     UT_EXIT
@next:  jsr     UT_NNEXT
        bcc     :+
        jmp     UT_EXIT
:       sta     name
        stx     name + 1
        lda     #<serase                ; ERASE M:ONE.TXT (Y/N)?
        ldx     #>serase
        jsr     CM_PUTS
        lda     fcb
        jsr     UT_FDRIVE
        jsr     UT_PUTDRV
        lda     name
        ldx     name + 1
        jsr     CM_FNAME
        lda     #<syneach
        ldx     #>syneach
        jsr     CM_PUTS
        jsr     CM_ASK
        bcc     @next                   ; kept
        lda     name
        ldx     name + 1
        jsr     delete
        bcc     @next
        cmp     #0
        bne     @next                   ; said: on to the next
        jsr     nofile
        bra     @next

; Delete (19) the 11 bytes at A/X on fcb's drive, fcb's password in the DMA
; buffer. Carry clear: done; carry set and A 0: nothing matched (nothing
; said), $FF: an error the BDOS said (UT_SAID's).
delete: sta     @src + 1
        stx     @src + 2
        lda     fcb
        sta     wfcb
        ldy     #10
@src:   lda     $FFFF,y
        sta     wfcb + 1,y
        dey
        bpl     @src
        ldy     #35 - 12                ; the rest 0
        lda     #0
:       sta     wfcb + 12,y
        dey
        bpl     :-
        lda     #<(fcb + 16)
        ldx     #>(fcb + 16)
        jsr     UT_PWSET
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_DELETE
        jsr     BDOS
        cmp     #$FF
        bne     @ok
        cpx     #0
        bne     @said
        lda     #0
        sec
        rts
@said:  jsr     UT_SAID
        lda     #$FF
        sec
        rts
@ok:    clc
        rts

; The tail's option list: CONFIRM.
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
        lda     #1
        sta     confirm
        bra     @next
@end:   cmp     #UT_OEND
        bne     @bad
@done:  rts
@bad:   lda     #<sopt
        ldx     #>sopt
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
        bcc     @second
        cmp     #0
        bne     @bad
        clc
        rts
@second:
        lda     #<sopt
        ldx     #>sopt
        bra     failsay
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
OPTAB:  .byte   "CONFIRM", 0, 0
sask:   .byte   "Enter filename: $"
serase: .byte   "ERASE $"
syn:    .byte   " (Y/N)?$"
syneach: .byte  " (Y/N)? $"
sopt:   .byte   "Missing delimiter or Unrecognized Option$"
sinv:   .byte   "Invalid file specification.$"
snofile: .byte  "No File$"
smem:   .byte   "Not Enough Memory$"

        .bss
confirm: .res   1                       ; 1: [CONFIRM]
name:   .res    2                       ; the name asked about
fcb:    .res    36                      ; the specification
wfcb:   .res    36                      ; the file deleted (and a second spec)

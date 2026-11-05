; PUT.COM (hw/oodos.md, "The RSXs, phase 5", 5b; the User's Guide's PUT, 5-77):
;
;   PUT CONSOLE {OUTPUT TO} FILE filespec {[options]}
;   PUT PRINTER {OUTPUT TO} FILE filespec {[options]}
;   PUT CONSOLE {OUTPUT TO} CONSOLE
;   PUT PRINTER {OUTPUT TO} PRINTER
;
; Options, which combine: ECHO (the console's default), NO ECHO (the
; printer's), FILTER, NO FILTER (the default), SYSTEM. The program parses the
; command, asks about a file there already (File already exists Delete it?
; (Y/N): N ends it; read-only: Read only.) and about a PUT of the kind already
; sending output to a file (Do you want another file? (Y/N): Y both get the
; output, N the old file closed), makes NAME.$$$ and gives it to its RSX
; (rsx/putrsx.s, attached by gencom.py) by 60. The old file is deleted only
; when the new one is complete. The ... TO CONSOLE and ... TO PRINTER forms end
; the PUT of the kind, its file closed.
;
; Errors as GET's (redir.inc): Invalid command, Invalid delimiter, Invalid
; option, Invalid option modifier (a value; ECHO with NO ECHO; FILTER with NO
; FILTER), End of line expected, Invalid file specification, Read only., the
; BDOS's (said), Insufficient Memory; the return code $FF00 after any.

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        stz     seen
        stz     flags
        jsr     RD_WORD
        bcs     @cmd
        lda     #<RD_CONSOLE
        ldx     #>RD_CONSOLE
        jsr     RD_IS
        lda     #0
        bcs     @kind
        lda     #<RD_PRINTER
        ldx     #>RD_PRINTER
        jsr     RD_IS
        lda     #1
        bcs     @kind
        lda     #<RD_AUXW
        ldx     #>RD_AUXW
        jsr     RD_IS
        bcc     @cmd
        jmp     RD_AUX
@kind:  sta     kind
        sta     pbask + 2
        sta     pbstop + 2
        sta     pbfile + 8
        jsr     RD_WORD
        bcs     @cmd
        lda     #<RD_OUTPUT
        ldx     #>RD_OUTPUT
        jsr     RD_IS
        bcc     device
        jsr     RD_WORD
        bcs     @cmd
        lda     #<RD_TO
        ldx     #>RD_TO
        jsr     RD_IS
        bcc     @cmd
        jsr     RD_WORD
        bcc     device
@cmd:   jmp     RD_CMD

; The device word: FILE, its own kind's device, AUXILIARY.
device: lda     #<RD_FILE
        ldx     #>RD_FILE
        jsr     RD_IS
        bcc     :+
        jmp     file
:       lda     #<RD_AUXW
        ldx     #>RD_AUXW
        jsr     RD_IS
        bcc     :+
        jmp     RD_AUX
:       lda     kind
        bne     @prn
        lda     #<RD_CONSOLE
        ldx     #>RD_CONSOLE
        bra     @dev
@prn:   lda     #<RD_PRINTER
        ldx     #>RD_PRINTER
@dev:   jsr     RD_IS
        bcs     stop
        jmp     RD_CMD

; ... TO CONSOLE, ... TO PRINTER: the PUT of the kind ends.
stop:   jsr     RD_END
        lda     #<pbstop
        ldx     #>pbstop
        ldy     #P_RSX
        jsr     BDOS
        jmp     UT_EXIT

file:   lda     #<fcb
        ldx     #>fcb
        jsr     RD_SPEC
        jsr     opts
        jsr     RD_END
        lda     kind                    ; ECHO the console's default, NO ECHO the printer's
        bne     :+
        lda     seen
        and     #3
        bne     :+
        lda     flags
        ora     #1
        sta     flags
:       lda     #<fcb
        ldx     #>fcb
        jsr     RD_DRIVE
        lda     #$FF
        ldy     #F_USERNUM
        jsr     BDOS
        sta     pbfile + 6
        ; NAME.TYP there already?
        ldx     #35
:       lda     fcb,x
        sta     ofcb,x
        dex
        bpl     :-
        ldx     #23
:       stz     ofcb + 12,x
        dex
        bpl     :-
        lda     #<(fcb + 16)
        ldx     #>(fcb + 16)
        jsr     UT_PWSET
        lda     #<ofcb
        ldx     #>ofcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     @there
        cpx     #0
        beq     @another
        jmp     said
@there: lda     ofcb + 9                ; read-only (t1')
        bpl     @ask
        lda     #<sro
        ldx     #>sro
        jmp     RD_FAIL
@ask:   lda     #<sexists
        ldx     #>sexists
        jsr     CM_PUTS
        jsr     CM_ASK
        bcs     @another
        jmp     UT_EXIT                 ; N: the end
@another:
        lda     #<pbask                 ; a PUT of the kind already?
        ldx     #>pbask
        ldy     #P_RSX
        jsr     BDOS
        cmp     #0
        bne     @make
        lda     #<sanother
        ldx     #>sanother
        jsr     CM_PUTS
        jsr     CM_ASK
        bcc     @replace
        lda     flags
        ora     #8                      ; beside it: both get the output
        sta     flags
        bra     @make
@replace:
        lda     #<pbstop
        ldx     #>pbstop
        ldy     #P_RSX
        jsr     BDOS
@make:  ldx     #11                     ; NAME.$$$, the drive and name
:       lda     fcb,x
        sta     tfcb,x
        sta     final,x
        dex
        bpl     :-
        lda     fcb + 9                 ; (attributes dropped)
        and     #$7F
        sta     final + 9
        lda     fcb + 10
        and     #$7F
        sta     final + 10
        ldx     #2
        lda     #'$'
:       sta     tfcb + 9,x
        dex
        bpl     :-
        ldx     #23
:       stz     tfcb + 12,x
        dex
        bpl     :-
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_MAKE
        jsr     BDOS
        cmp     #$FF
        bne     @made
        jmp     said
@made:  lda     flags
        sta     pbfile + 7
        lda     kind
        bne     @sprn
        lda     #<sputc
        ldx     #>sputc
        bra     @say
@sprn:  lda     #<sputp
        ldx     #>sputp
@say:   jsr     CM_PUTS
        lda     #<(final + 1)
        ldx     #>(final + 1)
        jsr     CM_FNAME
        lda     #<pbfile
        ldx     #>pbfile
        ldy     #P_RSX
        jsr     BDOS
        cmp     #0
        beq     @done
        lda     #<tfcb                  ; no RSX took it
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
        jmp     RD_MEM
@done:  jmp     UT_EXIT

said:   jsr     UT_SAID
        jmp     UT_EXIT

; The option list: ECHO, NO ECHO, FILTER, NO FILTER, SYSTEM.
opts:   jsr     UT_OPTS
        bne     :+
        rts
:       cmp     #UT_LONE
        beq     @next
        jmp     RD_DELIM
@next:  lda     #<OPTAB
        ldx     #>OPTAB
        jsr     UT_OPT
        bcs     @end
        ldy     UT_HASV
        bne     @mod
        tax
        lda     bits,x
        and     seen
        bne     @mod
        lda     bits,x
        ora     seen
        sta     seen
        lda     set,x
        ora     flags
        sta     flags
        bra     @next
@end:   cmp     #UT_OEND
        bne     @unk
        rts
@unk:   jmp     RD_OPT
@mod:   jmp     RD_MOD

        .include "cmd.inc"
        .include "util.inc"
        .include "redir.inc"

        .rodata
OPTAB:  .byte   "ECHO", 0, "NOECHO", 0, "FILTER", 0, "NOFILTER", 0, "SYSTEM", 0, 0
bits:   .byte   3, 3, 12, 12, 16        ; the pairs exclude each other
set:    .byte   1, 0, 2, 0, 4           ; the RSX's flags: ECHO, FILTER, SYSTEM
sro:    .byte   "Read only.$"
sexists: .byte  "File already exists Delete it? (Y/N)$"
sanother: .byte "Do you want another file? (Y/N)$"
sputc:  .byte   "Putting console output to file: $"
sputp:  .byte   "Putting printer output to file: $"

        .data
pbask:  .byte   RXF_PUTASK, 1, 0, 0
pbstop: .byte   RXF_PUTSTOP, 1, 0, 0
pbfile: .byte   RXF_PUTFILE, 4
        .word   tfcb, final
        .byte   0, 0                    ; the user, the flags
        .byte   0, 0                    ; the kind

        .bss
kind:   .res    1
seen:   .res    1
flags:  .res    1
fcb:    .res    36
ofcb:   .res    36
tfcb:   .res    36
final:  .res    12

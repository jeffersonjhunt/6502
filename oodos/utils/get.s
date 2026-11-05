; GET.COM (hw/oodos.md, "The RSXs, phase 5", 5b; the User's Guide's GET, 5-44):
;
;   GET {CONSOLE INPUT FROM} FILE filespec {[ECHO | NO ECHO | SYSTEM]}
;   GET {CONSOLE INPUT FROM} CONSOLE
;
; The program parses the command and opens the file; its RSX (rsx/getrsx.s,
; attached by the Makefile's gencom.py) does the redirection: given the file
; by 60 (RXF_GETFILE), it stays. GET CONSOLE (or CONSOLE INPUT FROM CONSOLE)
; ends the newest GET that serves input, by 60 (RXF_GETSTOP). The words are
; taken in full, as the Guide writes them; the options by any prefix that is
; one's alone (ours). AUXILIARY is refused: Auxiliary device redirection not
; implemented.
;
; Errors: Invalid command (words that make no form, or no file
; specification), Invalid delimiter (a second option list, one not closed),
; Invalid option, Invalid option modifier (a value, ECHO with NO ECHO, either
; with SYSTEM), End of line expected, Invalid file specification (152 refuses
; it, or a wildcard), No File, the BDOS's (said), Insufficient Memory (no RSX
; took the file: GET.COM without its RSX). The return code $FF00 after any.

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

O_ECHO   = 0
O_NOECHO = 1
O_SYSTEM = 2

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        stz     flags
        stz     seen
        jsr     RD_WORD
        bcs     @cmd
        lda     #<RD_AUXW
        ldx     #>RD_AUXW
        jsr     RD_IS
        bcs     @aux
        lda     #<RD_CONSOLE
        ldx     #>RD_CONSOLE
        jsr     RD_IS
        bcc     device                  ; FILE, or no form
        jsr     RD_WORD                 ; CONSOLE alone: GET CONSOLE
        bcs     stop
        lda     #<RD_INPUT
        ldx     #>RD_INPUT
        jsr     RD_IS
        bcc     @cmd
        jsr     RD_WORD
        bcs     @cmd
        lda     #<RD_FROM
        ldx     #>RD_FROM
        jsr     RD_IS
        bcc     @cmd
        jsr     RD_WORD
        bcs     @cmd
        bra     device
@aux:   jmp     RD_AUX
@cmd:   jmp     RD_CMD

; The device word in RD_W: FILE, CONSOLE, AUXILIARY.
device: lda     #<RD_FILE
        ldx     #>RD_FILE
        jsr     RD_IS
        bcs     file
        lda     #<RD_CONSOLE
        ldx     #>RD_CONSOLE
        jsr     RD_IS
        bcs     stop
        lda     #<RD_AUXW
        ldx     #>RD_AUXW
        jsr     RD_IS
        bcc     :+
        jmp     RD_AUX
:       jmp     RD_CMD

; GET CONSOLE: the newest GET serving input ends.
stop:   jsr     RD_END
        lda     #<pbstop
        ldx     #>pbstop
        ldy     #P_RSX
        jsr     BDOS
        jmp     UT_EXIT

; GET FILE filespec [options].
file:   lda     #<fcb
        ldx     #>fcb
        jsr     RD_SPEC
        jsr     opts
        jsr     RD_END
        lda     #<fcb
        ldx     #>fcb
        jsr     RD_DRIVE
        lda     #$FF                    ; the user it is read in
        ldy     #F_USERNUM
        jsr     BDOS
        sta     pbfile + 4
        lda     #<(fcb + 16)
        ldx     #>(fcb + 16)
        jsr     UT_PWSET
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     @open
        cpx     #0
        bne     @said
        lda     #<snofile
        ldx     #>snofile
        jmp     RD_FAIL
@said:  jsr     UT_SAID
        jmp     UT_EXIT
@open:  lda     flags
        sta     pbfile + 5
        lda     #<sget
        ldx     #>sget
        jsr     CM_PUTS
        lda     #<(fcb + 1)
        ldx     #>(fcb + 1)
        jsr     CM_FNAME
        lda     #<pbfile
        ldx     #>pbfile
        ldy     #P_RSX
        jsr     BDOS
        cmp     #0
        beq     @done
        jmp     RD_MEM
@done:  jmp     UT_EXIT

; The option list: ECHO, NO ECHO, SYSTEM, one or the other.
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
        tay
        and     seen
        bne     @mod                    ; given twice, or with its other
        tya
        ora     seen
        sta     seen
        cpx     #O_NOECHO
        bne     :+
        lda     flags
        ora     #1
        sta     flags
:       cpx     #O_SYSTEM
        bne     @next
        lda     flags
        ora     #2
        sta     flags
        bra     @next
@end:   cmp     #UT_OEND
        bne     @unk
        lda     seen                    ; SYSTEM with ECHO or NO ECHO
        cmp     #7
        beq     @mod
        rts
@unk:   jmp     RD_OPT
@mod:   jmp     RD_MOD

        .include "cmd.inc"
        .include "util.inc"
        .include "redir.inc"

        .rodata
OPTAB:  .byte   "ECHO", 0, "NOECHO", 0, "SYSTEM", 0, 0
bits:   .byte   3, 3, 4                 ; ECHO and NO ECHO exclude each other; SYSTEM both
snofile: .byte  "No File$"
sget:   .byte   "Getting console input from file: $"

        .data
pbstop: .byte   RXF_GETSTOP, 0
pbfile: .byte   RXF_GETFILE, 2
        .word   fcb
        .byte   0, 0                    ; the user, the flags

        .bss
flags:  .res    1
seen:   .res    1
fcb:    .res    36


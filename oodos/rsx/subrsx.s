; SUBMIT's RSX (hw/oodos.md, "The RSXs, phase 5", SUBMIT and 5c's decisions;
; the User's Guide's SUBMIT, 5-109): a SUBMIT file's lines, expanded by
; SUBMIT.COM into a temporary file (SUBnn.$$$ in user 0 on the temporary drive),
; as console input. Attached to SUBMIT.COM by gencom.py; its remove flag $FF in
; the file, so it goes after SUBMIT.COM's run unless SUBMIT.COM gives it the file
; by 60 (RXF_SUBFILE: the FCB, opened), which makes it stay. RXF_SUBASK: A = 0 if
; it serves a file (SUBMIT.COM deletes stale temporary files only when none do).
;
; Given its file it serves at once (rxfile.inc reads it):
;
;   the way to the prompt (49's write to $19 with bit 6 clear while it is
;               clear: the CCP's PROMPT, not a command's end, which may have
;               more of a ! line after it) the < lines left skipped, and
;               WARNING: Program input ignored said once, before the prompt
;               (not after a ^C)
;   the prompt  (10 with $19 bit 7 set, bit 6 clear) the next command line,
;               shown and ended by CR as typed (< lines still there skipped as
;               above); a ^C's end ($FFFE, 108) or the file's end: its file
;               deleted, done, the keyboard
;   a program's input (or a built-in's question: bit 6 set) a < line: 10 the
;               rest of it (A/X 0: the DMA buffer's characters first, as GET),
;               1 and 6's $FF and $FD a byte at a time, then CR (< alone: a
;               CR); 11 and 6's $FE as GET's, by console mode bits 8 and 9 (both
;               set: no redirection). No < line next: the keyboard, the line
;               kept for the prompt
;
; Its own newer SUBMITs and GETs, stacked below it, serve before it.

        .setcpu "65C02"
        .include "oodos.inc"
        .macpack longbranch

        OODOS_HEADER
        RSX_PREFIX "SUBMIT", entry, $FF

CR      = $0D
LF      = $0A
BS      = $08
TAB     = $09
CTLZ    = $1A
LMAX    = 130                           ; a line: SUBMIT.COM's 128 and a '<'

M_IDLE   = 0
M_ACTIVE = 1
M_DONE   = 2

        .code
entry:  sta     sa
        stx     sx
        sty     sy
        cpy     #P_RSX
        jeq     rsx
        lda     mode
        cmp     #M_ACTIVE
        bne     pass
        cpy     #S_SCB
        jeq     scb
        cpy     #C_STAT
        jeq     status
        cpy     #C_RAWIO
        jeq     raw
        stz     lastst                  ; any other call breaks a run of status calls
        cpy     #C_READSTR
        jeq     line
        cpy     #C_READ
        jeq     char
pass:   lda     sa
        ldx     sx
        ldy     sy
        jmp     PREFIX + RSX_NEXT

; 60: SUBMIT.COM's file; whether one is served.
rsx:    lda     sa
        ldx     sx
        jsr     RX_SETP
        ldy     #0
        jsr     RX_PEEK
        cmp     #RXF_SUBASK
        bne     @file
        lda     mode
        cmp     #M_ACTIVE
        bne     pass
        jmp     zero
@file:  cmp     #RXF_SUBFILE
        bne     pass
        lda     mode
        bne     pass                    ; only an RSX given no file yet
        ldy     #2
        jsr     RX_PEEK
        sta     @src + 1
        iny
        jsr     RX_PEEK
        sta     @src + 2
        ldy     #35
@src:   lda     $FFFF,y
        sta     fcb,y
        dey
        bpl     @src
        stz     user                    ; user 0 (decided)
        lda     #128
        sta     bidx
        stz     eof
        stz     prevcr
        stz     have
        stz     inlt
        lda     #M_ACTIVE
        sta     mode
        jsr     RX_STAY
zero:   lda     #0
        tax
        rts

; 49: the CCP's writes to $19. Bit 6 clear written while it is clear: the
; CCP's way to the prompt (a command's end clears it from set). The < lines
; left skipped and the warning said now, before the prompt; after a ^C
; ($FFFE) nothing, the prompt ends it.
scb:    lda     sa
        ldx     sx
        jsr     RX_SETP
        jsr     RX_PB49
        bcc     @pass
        and     #RX_CMD
        bne     @pass
        ldx     #SCB_CCPST              ; and clear now (the write isn't made yet)
        jsr     RX_SCBGET
        and     #RX_CMD
        bne     @pass
        lda     #$FF
        tax
        ldy     #P_CODE
        jsr     RX_BDOS
        cmp     #$FE
        bne     :+
        cpx     #$FF
        beq     @pass
:       jsr     skipin
@pass:  jmp     pass

; The < lines next skipped (a line 1 began among them), the warning said if
; there were any; carry set at the file's end.
skipin: stz     skipped
@peek:  jsr     peekline
        bcs     @end
        lda     lbuf
        cmp     #'<'
        clc                             ; a command: not the end
        bne     @end
        stz     have                    ; program input left over: skipped
        stz     inlt
        lda     #1
        sta     skipped
        bra     @peek
@end:   php
        lda     skipped
        beq     :+
        lda     #<swarn
        ldx     #>swarn
        jsr     say
:       plp
        rts

; 10: the prompt's command line, or a < line for a program or a question.
line:   ldx     #SCB_CCPST
        jsr     RX_SCBGET
        and     #RX_CCP | RX_CMD
        cmp     #RX_CCP
        jne     input
        lda     #$FF                    ; the prompt: a ^C's end first
        tax
        ldy     #P_CODE
        jsr     RX_BDOS
        cmp     #$FE
        bne     @next
        cpx     #$FF
        bne     @next
        jsr     endsub
        jmp     pass
@next:  jsr     skipin                  ; unreachable in effect: the way to the
                                        ; prompt (scb) skipped them before it, and
                                        ; after a ^C the prompt ends the SUBMIT
                                        ; above; kept so a line is never given
                                        ; from a < line
        bcc     :+
        jsr     endsub                  ; the file's end: the keyboard
        jmp     pass
:       stz     lfrom
        jmp     give

; Program input (or a built-in's question): a < line's rest, or the keyboard.
input:  jsr     redir
        bcs     :+
        jmp     pass
:       lda     inlt                    ; the rest of a line 1 began
        bne     :+
        lda     #1
        sta     lpos
:       lda     lpos
        sta     lfrom
        stz     inlt
        ; falls through
; The line from lfrom to the caller's buffer (A/X, or the DMA address if 0:
; its own characters first), shown and ended by CR; the line taken.
give:   stz     initial
        lda     sa
        ora     sx
        bne     @addr
        inc     initial
        ldx     #$3C
        jsr     RX_SCBGET
        sta     sa
        ldx     #$3D
        jsr     RX_SCBGET
        sta     sx
@addr:  lda     sa
        ldx     sx
        jsr     RX_SETP
        lda     sa
        sta     @st + 1
        lda     sx
        sta     @st + 2
        ldy     #0
        jsr     RX_PEEK
        bne     :+
        lda     #1
:       sta     cmax
        stz     ccount
        lda     initial
        beq     @copy
@pre:   ldx     ccount
        cpx     cmax
        bcs     @copy
        txa
        clc
        adc     #2
        tay
        jsr     RX_PEEK
        beq     @copy
        cmp     #CR
        beq     @done                   ; a CR among them: the line ends there
        inc     ccount
        bra     @pre
@copy:  ldx     lfrom
@c:     cpx     llen
        bcs     @taken
        ldy     ccount
        cpy     cmax
        bcs     @taken
        lda     lbuf,x
        pha
        tya
        clc
        adc     #2
        tay
        pla
@st:    sta     $FFFF,y
        inc     ccount
        inx
        bra     @c
@taken: stz     have
@done:  lda     ccount                  ; the count, then the line shown and CR
        ldy     #1
        jsr     @put
        ldy     #2
@show:  tya
        sec
        sbc     #2
        cmp     ccount
        bcs     @cr
        jsr     RX_PEEK
        phy
        ldy     #C_WRITE
        jsr     RX_BDOS
        ply
        iny
        bra     @show
@cr:    lda     #CR
        ldy     #C_WRITE
        jsr     RX_BDOS
        jmp     zero
@put:   pha
        lda     @st + 1
        sta     @p + 1
        lda     @st + 2
        sta     @p + 2
        pla
@p:     sta     $FFFF,y
        rts

; 1: a byte of a < line, echoed as 1 echoes; at its end CR.
char:   jsr     onebyte
        bcs     :+
        jmp     pass
:       sta     ch
        cmp     #$20
        bcc     @ctl
        cmp     #$7F
        bcs     @ret
        bra     @echo
@ctl:   cmp     #CR
        beq     @echo
        cmp     #LF
        beq     @echo
        cmp     #BS
        beq     @echo
        cmp     #TAB
        bne     @ret
@echo:  ldy     #C_WRITE
        jsr     RX_BDOS
@ret:   lda     ch
        ldx     #0
        rts

; 6: $FF and $FD a byte, raw; $FE the status; anything else output.
raw:    lda     sa
        cmp     #$FE
        beq     status
        stz     lastst
        cmp     #$FD
        bcc     @out
        jsr     onebyte
        bcc     @out
        ldx     #0
        rts
@out:   jmp     pass

; 11, 6's $FE: with a < line to give, by console mode bits 8 and 9, as GET.
status: jsr     redir
        bcs     @st
        stz     lastst
        jmp     pass
@st:    ldx     #$34
        jsr     RX_SCBGET
        and     #3
        cmp     #1
        beq     @true
        cmp     #2
        beq     @false
        lda     lastst
        bne     @true2
        lda     #1
        sta     lastst
@false: lda     #0
        bra     @ret
@true2: stz     lastst
@true:  lda     #1
        ldy     sy
        cpy     #C_RAWIO
        bne     @ret
        lda     #$FF
@ret:   ldx     #0
        rts

; Carry set, A the byte, if a < line gives a byte: its next, or CR at its end
; (the line then taken); carry clear if none (the keyboard).
onebyte:
        lda     inlt
        bne     @in
        jsr     redir
        bcc     @none
        lda     #1
        sta     lpos
        sta     inlt
@in:    ldx     lpos
        cpx     llen
        bcs     @end
        lda     lbuf,x
        inc     lpos
        sec
        rts
@end:   stz     inlt
        stz     have
        lda     #CR
        sec
        rts
@none:  clc
        rts

; Carry set if a < line is next for a program (or one 1 began), and the console
; mode doesn't say no redirection (bits 8 and 9 both set); the file's end ends
; it (carry clear).
redir:  ldx     #$34
        jsr     RX_SCBGET
        and     #3
        cmp     #3
        beq     @no
        lda     inlt
        bne     @yes
        jsr     peekline
        bcc     :+
        jsr     endsub
        bra     @no
:       lda     lbuf
        cmp     #'<'
        bne     @no
@yes:   sec
        rts
@no:    clc
        rts

; The next line in lbuf (llen), read if none is: carry set at the file's end.
peekline:
        lda     have
        bne     @have
        stz     llen
@c:     jsr     nextch
        bcs     @eof
        cmp     #CR
        beq     @got
        cmp     #LF
        beq     @got
        ldx     llen
        cpx     #LMAX
        bcs     @c
        sta     lbuf,x
        inc     llen
        bra     @c
@eof:   lda     llen                    ; a last line without its CR, or the end
        beq     @end
@got:   lda     #1
        sta     have
@have:  clc
        rts
@end:   sec
        rts

; The end: its file deleted (user 0), done, out at the next warm boot.
endsub: lda     mode
        cmp     #M_ACTIVE
        bne     @done
        lda     #0
        jsr     RX_SAVE
        ldx     #11
:       lda     fcb,x
        sta     dfcb,x
        dex
        bpl     :-
        ldx     #23
:       stz     dfcb + 12,x
        dex
        bpl     :-
        lda     #<dfcb
        ldx     #>dfcb
        ldy     #F_DELETE
        jsr     RX_BDOS
        jsr     RX_RESTORE
        lda     #M_DONE
        sta     mode
        jsr     RX_REMOVE
@done:  rts

; The 0-ended string at A/X to the console.
say:    sta     @l + 1
        stx     @l + 2
        ldx     #0
@l:     lda     $FFFF,x
        beq     @d
        phx
        ldy     #C_WRITE
        jsr     RX_BDOS
        plx
        inx
        bra     @l
@d:     rts

        .include "rsx.inc"
        .include "rxfile.inc"

        .rodata
swarn:  .byte   CR, LF, "WARNING: Program input ignored", 0 ; the prompt's CR LF after

        .segment "RSXHDR"
PREFIX  = * - RSX_PREFIX_LEN

        .data                           ; in the file: as loaded (BSS isn't cleared)
mode:   .byte   M_IDLE
lastst: .byte   0
have:   .byte   0
inlt:   .byte   0
skipped: .byte  0
llen:   .byte   0
lpos:   .byte   0
lfrom:  .byte   0
cmax:   .byte   0
ccount: .byte   0
initial: .byte  0
sa:     .byte   0
sx:     .byte   0
sy:     .byte   0
dfcb:   .res    36
lbuf:   .res    LMAX

; GET's RSX (hw/oodos.md, "The RSXs, phase 5", GET; the User's Guide's GET,
; 5-44): console input from a file. Attached to GET.COM by gencom.py; its
; remove flag $FF in the file, so it goes after GET.COM's run unless GET.COM
; gives it a file by 60 (RXF_GETFILE: its FCB, opened; the user; the flags,
; bit 0 NO ECHO, bit 1 SYSTEM), which makes it stay (RX_STAY) until it is done.
;
;   armed       waiting for the next command: the first start of a command
;               ($19 bit 6 set) whose count isn't GET.COM's makes it active
;   active      the command's console input from the file, to the command's
;               end ($19 bit 6 clear); then done
;   system      [SYSTEM]: the CCP's lines and every program's input, at once
;   done        at the file's end (^Z, or its last record) or RXF_GETSTOP
;               (GET CONSOLE): its remove flag $FF, every call passed on
;
; While it serves: 10 a line from the file (to CR, or a lone LF; an LF after
; CR dropped), shown and ended by CR as typed unless NO ECHO; 1 a byte, echoed
; as 1 echoes unless NO ECHO; 6's $FF and $FD a byte, raw, never echoed; 11
; and 6's $FE by console mode bits 8 and 9 (the SCB's $34; the Programmer's
; Guide's 2.2.1): 0 conditional (false, but true for a status call straight
; after another), bit 8 alone true, bit 9 alone false, both: no redirection.
; A call it doesn't serve, or one at the file's end, goes on to the console.

        .setcpu "65C02"
        .include "oodos.inc"
        .macpack longbranch

        OODOS_HEADER
        RSX_PREFIX "GET", entry, $FF

CR      = $0D
LF      = $0A
BS      = $08
TAB     = $09
CTLZ    = $1A

M_IDLE   = 0
M_ARMED  = 1
M_ACTIVE = 2
M_SYSTEM = 3
M_DONE   = 4

        .code
entry:  sta     sa
        stx     sx
        sty     sy
        cpy     #C_STAT
        jeq     status
        cpy     #C_RAWIO
        jeq     raw
        stz     lastst                  ; any other call breaks a run of status calls
        cpy     #S_SCB
        jeq     scb
        cpy     #P_RSX
        jeq     rsx
        cpy     #C_READSTR
        bne     :+
        jmp     line
:       cpy     #C_READ
        bne     pass
        jmp     char
pass:   lda     sa
        ldx     sx
        ldy     sy
        jmp     PREFIX + RSX_NEXT

; 49: the CCP's writes to $19, as they go down the chain.
scb:    lda     mode
        cmp     #M_ARMED
        beq     @look
        cmp     #M_ACTIVE
        bne     pass
@look:  lda     sa
        ldx     sx
        jsr     RX_SETP
        jsr     RX_PB49
        bcc     pass
        tax
        lda     mode
        cmp     #M_ARMED
        bne     @active
        txa                             ; armed: the next command's start (its own
        and     #RX_CMD                 ; command's bit 6 clears at the warm boot first)
        beq     pass
        lda     #M_ACTIVE
        sta     mode
        bra     pass
@active: txa                            ; active: its end
        and     #RX_CMD
        bne     pass
        jsr     done
        bra     pass

; 60: GET.COM's file, or GET CONSOLE.
rsx:    lda     sa
        ldx     sx
        jsr     RX_SETP
        ldy     #0
        jsr     RX_PEEK
        cmp     #RXF_GETFILE
        beq     @file
        cmp     #RXF_GETSTOP
        bne     pass
        lda     mode                    ; the newest serving one ends
        beq     pass
        cmp     #M_DONE
        beq     pass
        jsr     done
        jmp     zero
@file:  lda     mode                    ; only an RSX given no file yet
        bne     pass
        ldy     #2                      ; word 1: the FCB
        jsr     RX_PEEK
        sta     @src + 1
        iny
        jsr     RX_PEEK
        sta     @src + 2
        iny                             ; word 2: the user, the flags
        jsr     RX_PEEK
        sta     user
        iny
        jsr     RX_PEEK
        sta     flags
        ldy     #35
@src:   lda     $FFFF,y
        sta     fcb,y
        dey
        bpl     @src
        lda     #128                    ; nothing read yet
        sta     bidx
        stz     eof
        stz     prevcr
        lda     #M_ARMED
        tax
        lda     flags
        and     #2
        beq     :+
        ldx     #M_SYSTEM
:       stx     mode
        jsr     RX_STAY
zero:   lda     #0
        tax
        rts

; 11, 6's $FE: the status, by the console mode's bits 8 and 9.
status: jsr     serving
        bcs     @st
        stz     lastst
        jmp     pass
@st:    cmp     #1                      ; A: the bits
        beq     @true
        cmp     #2
        beq     @false
        lda     lastst                  ; conditional: true only straight after another
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

; 6: $FF and $FD a byte, raw; $FE the status; anything else output.
raw:    lda     sa
        cmp     #$FE
        beq     status
        stz     lastst
        cmp     #$FD
        bcc     @out
        jsr     serving
        bcc     @out
        jsr     nextch
        bcs     @end
        ldx     #0
        rts
@end:   jsr     done
@out:   jmp     pass

; 1: a byte, echoed as 1 echoes.
char:   jsr     serving
        bcc     @pass
        jsr     nextch
        bcs     @end
        sta     ch
        lda     flags
        lsr     a
        bcs     @ret                    ; NO ECHO
        lda     ch
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
@end:   jsr     done
@pass:  jmp     pass

; 10: a line from the file into the caller's buffer (A/X, or the DMA
; address if 0): its size, the count, the characters. With 0, the characters
; already in the buffer, to a 0 (or its size), are taken as typed first, and a
; CR among them ends the line there, as the BDOS's 10 does.
line:   jsr     serving
        bcs     :+
        jmp     pass
:       stz     initial
        lda     sa
        ora     sx
        bne     @addr
        inc     initial
        ldx     #$3C                    ; the DMA address
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
:       sta     lmax
        stz     lcount
        lda     initial
        beq     @next
@pre:   ldx     lcount                  ; the buffer's own characters first
        cpx     lmax
        bcs     @next
        txa
        clc
        adc     #2
        tay
        jsr     RX_PEEK
        beq     @next
        cmp     #CR
        beq     @done
        inc     lcount
        bra     @pre
@next:  jsr     nextch
        bcs     @eof
        cmp     #CR
        beq     @done
        cmp     #LF
        beq     @done
        ldx     lcount
        cpx     lmax
        bcs     @next                   ; past its size: dropped
        inx
        stx     lcount
        inx
        txa
        tay
        lda     ch
@st:    sta     $FFFF,y
        bra     @next
@eof:   lda     lcount                  ; the file's end: what was read, or the console
        bne     @done
        jsr     done
        jmp     pass
@done:  lda     lcount
        ldy     #1
        jsr     @put
        lda     flags                   ; shown as typed, then CR
        lsr     a
        bcs     @ret
        ldy     #2
@show:  tya
        sec
        sbc     #2
        cmp     lcount
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
@ret:   jmp     zero
@put:   sta     ch
        lda     @st + 1
        sta     @p + 1
        lda     @st + 2
        sta     @p + 2
        lda     ch
@p:     sta     $FFFF,y
        rts

; Carry set if it serves input now (active or system, the console mode's
; bits 8 and 9 not both set), A those bits.
serving:
        lda     mode
        cmp     #M_ACTIVE
        beq     @bits
        cmp     #M_SYSTEM
        beq     @bits
        clc
        rts
@bits:  ldx     #$34
        jsr     RX_SCBGET
        and     #3
        cmp     #3
        beq     @no
        sec
        rts
@no:    clc
        rts

; The file's next input byte, A (and ch): an LF after CR dropped; carry set
; at its end.
nextch: jsr     getc
        bcs     @end
        cmp     #LF
        bne     @take
        ldx     prevcr
        stz     prevcr
        bne     nextch
@take:  stz     prevcr
        cmp     #CR
        bne     @ret
        ldx     #1
        stx     prevcr
@ret:   sta     ch
        clc
@end:   rts

; The file's next byte: carry set at ^Z or the end of its last record.
getc:   lda     eof
        bne     @end
        lda     bidx
        bpl     @have
        lda     user                    ; a record, in the caller's state put back after
        jsr     RX_SAVE
        lda     #<buf
        ldx     #>buf
        ldy     #F_DMAOFF
        jsr     RX_BDOS
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_READ
        jsr     RX_BDOS
        pha
        jsr     RX_RESTORE
        pla
        bne     @last
        stz     bidx
@have:  ldx     bidx
        lda     buf,x
        cmp     #CTLZ
        beq     @last
        inc     bidx
        clc
        rts
@last:  lda     #1
        sta     eof
@end:   sec
        rts

; Done: every call passed on, and the next warm boot takes it out.
done:   lda     #M_DONE
        sta     mode
        jmp     RX_REMOVE

        .include "rsx.inc"

        .segment "RSXHDR"
PREFIX  = * - RSX_PREFIX_LEN

        .data                           ; in the file: 0 as loaded (BSS isn't cleared)
mode:   .byte   M_IDLE
flags:  .byte   0
user:   .byte   0
lastst: .byte   0
eof:    .byte   0
prevcr: .byte   0
bidx:   .byte   128
ch:     .byte   0
lmax:   .byte   0
lcount: .byte   0
initial: .byte  0
sa:     .byte   0
sx:     .byte   0
sy:     .byte   0
fcb:    .res    36
buf:    .res    128

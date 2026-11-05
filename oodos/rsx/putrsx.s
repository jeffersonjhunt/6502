; PUT's RSX (hw/oodos.md, "The RSXs, phase 5", PUT; the User's Guide's PUT,
; 5-77): console or printer output to a file. Attached to PUT.COM by
; gencom.py; its remove flag $FF in the file, so it goes after PUT.COM's run
; unless PUT.COM gives it a file by 60 (RXF_PUTFILE), which makes it stay.
;
; The block's words: the FCB of NAME.$$$, made and open; the final name (a
; drive byte and 11); the user, then the flags (bit 0 ECHO, bit 1 FILTER, bit
; 2 SYSTEM, bit 3 beside another PUT of its kind: every call passed on after
; it is written); the kind (0 console: 2, 6's output, 9, 111; 1 printer: 5,
; 112). Armed, it waits for the next command (the first start, $19 bit 6 set,
; whose count isn't PUT.COM's) and serves it to its end (bit 6 clear); with
; SYSTEM it serves at once, until RXF_PUTSTOP of its kind. RXF_PUTASK: A = 0
; if it is a PUT of the kind given, still sending output to a file.
;
; What it serves goes to the file as the program sent it (a tab as a tab, 9's
; string to the SCB's delimiter); with FILTER, a control character but CR and
; LF as ^ and a letter. Then the call goes on to the device if ECHO (or
; beside another PUT); else it returns. Under NO ECHO for the console, a call
; for input that follows output shows PUT> first. The file is a record at a
; time from its buffer; at each command's end it is closed (16, so the warm
; boot's 98 doesn't free its blocks), and at the end of what it serves its
; last record is written ^Z-padded, it is closed, NAME.TYP deleted and
; NAME.$$$ renamed to it: then PUT ERROR: FILE ERASED if it can't be closed.

        .setcpu "65C02"
        .include "oodos.inc"
        .macpack longbranch

        OODOS_HEADER
        RSX_PREFIX "PUT", entry, $FF

CR      = $0D
LF      = $0A
CTLZ    = $1A

M_IDLE   = 0
M_ARMED  = 1
M_ACTIVE = 2
M_SYSTEM = 3
M_DONE   = 4

F_ECHO   = 1
F_FILTER = 2
F_SYS    = 4
F_BESIDE = 8

        .code
entry:  sta     sa
        stx     sx
        sty     sy
        cpy     #S_SCB
        jeq     scb
        cpy     #P_RSX
        jeq     rsx
        jsr     serving
        bcc     pass
        lda     kind
        bne     lstout
        cpy     #C_WRITE
        jeq     w2
        cpy     #C_WRITESTR
        jeq     w9
        cpy     #C_WRITEBLK
        jeq     w111
        cpy     #C_RAWIO
        jeq     w6
        cpy     #C_READ
        beq     input
        cpy     #C_READSTR
        beq     input
pass:   lda     sa
        ldx     sx
        ldy     sy
        jmp     PREFIX + RSX_NEXT
lstout: cpy     #L_WRITE
        jeq     w5
        cpy     #L_WRITEBLK
        jeq     w112
        bra     pass

; Input under NO ECHO, after output: PUT> first.
input:  lda     flags
        and     #F_ECHO | F_BESIDE
        bne     pass
        lda     lastout
        beq     pass
        stz     lastout
        ldx     #0
@c:     lda     sprompt,x
        beq     pass
        phx
        ldy     #C_WRITE
        jsr     RX_BDOS
        plx
        inx
        bra     @c

; 2 and 5: A.
w2:
w5:     lda     sa
        jsr     putc
        jmp     after
; 6: output if A < $FD; input else.
w6:     lda     sa
        cmp     #$FD
        bcc     w2
        cmp     #$FE
        jeq     pass
        jmp     input
; 9: to the delimiter.
w9:     ldx     #$37
        jsr     RX_SCBGET
        sta     delim
        lda     sa
        ldx     sx
        jsr     RX_SETP
        ldy     #0
@c:     jsr     RX_PEEK
        cmp     delim
        jeq     after
        phy
        jsr     putc
        ply
        iny
        bne     @c
        inc     RX_PEEK + 2
        bra     @c
; 111 and 112: the block's address and length.
w111:
w112:   lda     sa
        ldx     sx
        jsr     RX_SETP
        ldy     #0
        jsr     RX_PEEK
        sta     bp
        iny
        jsr     RX_PEEK
        sta     bp + 1
        iny
        jsr     RX_PEEK
        sta     bn
        iny
        jsr     RX_PEEK
        sta     bn + 1
        lda     bp
        ldx     bp + 1
        jsr     RX_SETP
        ldy     #0
@c:     lda     bn
        ora     bn + 1
        jeq     after
        jsr     RX_PEEK
        phy
        jsr     putc
        ply
        lda     bn
        bne     :+
        dec     bn + 1
:       dec     bn
        iny
        bne     @c
        inc     RX_PEEK + 2
        bra     @c
; After output: on to the device, or back.
after:  lda     #1
        sta     lastout
        lda     flags
        and     #F_ECHO | F_BESIDE
        jne     pass
        lda     #0
        tax
        rts

; 49: the CCP's writes to $19.
scb:    lda     mode
        cmp     #M_ARMED
        bcc     @pass
        cmp     #M_DONE
        bcs     @pass
        lda     sa
        ldx     sx
        jsr     RX_SETP
        jsr     RX_PB49
        bcc     @pass
        tax
        lda     mode
        cmp     #M_ARMED
        bne     @end
        txa                             ; armed: the next command's start (its own
        and     #RX_CMD                 ; command's bit 6 clears at the warm boot first)
        beq     @pass
        lda     #M_ACTIVE
        sta     mode
        bra     @pass
@end:   txa                             ; a command's end: closed; active, the end
        and     #RX_CMD
        bne     @pass
        lda     mode
        cmp     #M_ACTIVE
        bne     @sys
        jsr     finish
        bra     @pass
@sys:   jsr     closef
@pass:  jmp     pass

; 60: PUT.COM's file, a stop, a question.
rsx:    lda     sa
        ldx     sx
        jsr     RX_SETP
        ldy     #0
        jsr     RX_PEEK
        cmp     #RXF_PUTFILE
        beq     @file
        cmp     #RXF_PUTSTOP
        beq     @stop
        cmp     #RXF_PUTASK
        bne     @pass
        jsr     mykind                  ; a PUT of the kind, sending to a file?
        bcc     @pass
        jmp     zero
@stop:  jsr     mykind
        bcc     @pass
        jsr     finish
        jmp     zero
@pass:  jmp     pass
@file:  lda     mode
        bne     @pass
        ldy     #2
        jsr     RX_PEEK
        sta     @fcb + 1
        iny
        jsr     RX_PEEK
        sta     @fcb + 2
        iny
        jsr     RX_PEEK
        sta     @nm + 1
        iny
        jsr     RX_PEEK
        sta     @nm + 2
        iny
        jsr     RX_PEEK
        sta     user
        iny
        jsr     RX_PEEK
        sta     flags
        iny
        jsr     RX_PEEK
        sta     kind
        ldy     #35
@fcb:   lda     $FFFF,y
        sta     fcb,y
        dey
        bpl     @fcb
        ldy     #11
@nm:    lda     $FFFF,y
        sta     final,y
        dey
        bpl     @nm
        stz     bidx
        stz     lastout
        ldx     #M_ARMED
        lda     flags
        and     #F_SYS
        beq     :+
        ldx     #M_SYSTEM
:       stx     mode
        jsr     RX_STAY
zero:   lda     #0
        tax
        rts

; Carry set if it serves output now: active or system.
serving:
        lda     mode
        cmp     #M_ACTIVE
        beq     @yes
        cmp     #M_SYSTEM
        beq     @yes
        clc
        rts
@yes:   sec
        rts

; Carry set if it is configured, not done, and of the kind in the block's
; word 1 (RX_SETP'd).
mykind: lda     mode
        beq     @no
        cmp     #M_DONE
        beq     @no
        ldy     #2
        jsr     RX_PEEK
        cmp     kind
        bne     @no
        sec
        rts
@no:    clc
        rts

; A to the file: FILTER's ^ and a letter for a control character but CR, LF.
putc:   sta     ch
        lda     flags
        and     #F_FILTER
        beq     @raw
        lda     ch
        cmp     #$20
        bcs     @raw
        cmp     #CR
        beq     @raw
        cmp     #LF
        beq     @raw
        lda     #'^'
        jsr     putb
        lda     ch
        ora     #$40
        jmp     putb
@raw:   lda     ch
        ; falls through
; A to the record buffer, the record written when full.
putb:   ldx     bidx
        sta     buf,x
        inx
        stx     bidx
        bpl     @done
        jsr     writer
        stz     bidx
@done:  rts

; The buffer written as the file's next record (21).
writer: lda     user
        jsr     RX_SAVE
        lda     #<buf
        ldx     #>buf
        ldy     #F_DMAOFF
        jsr     RX_BDOS
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_WRITE
        jsr     RX_BDOS
        jmp     RX_RESTORE

; Closed (16): its directory entry as far as written.
closef: lda     user
        jsr     RX_SAVE
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_CLOSE
        jsr     RX_BDOS
        sta     ch
        jmp     RX_RESTORE

; The end: the last record ^Z-padded, closed, NAME.TYP deleted, NAME.$$$
; renamed to it; PUT ERROR: FILE ERASED if it couldn't be closed. Done.
finish: lda     bidx
        beq     @close
@pad:   lda     #CTLZ
        jsr     putb
        lda     bidx
        bne     @pad
@close: jsr     closef
        lda     ch
        cmp     #$FF
        beq     @erased
        lda     user
        jsr     RX_SAVE
        lda     #<buf                   ; NAME.$$$ still there? (a close of a file
        ldx     #>buf                   ; written nothing to may not look)
        ldy     #F_DMAOFF
        jsr     RX_BDOS
        ldx     #11
@s:     lda     fcb,x
        sta     dfcb,x
        dex
        bpl     @s
        stz     dfcb + 12
        lda     #<dfcb
        ldx     #>dfcb
        ldy     #F_SFIRST
        jsr     RX_BDOS
        cmp     #$FF
        bne     @there
        jsr     RX_RESTORE
        bra     @erased
@there: ldx     #11                     ; the rename's FCB: NAME.$$$, then NAME.TYP at +16
@n:     lda     fcb,x
        sta     rfcb,x
        lda     final,x
        sta     rfcb + 16,x
        sta     dfcb,x
        dex
        bpl     @n
        ldx     #23
@z:     stz     dfcb + 12,x
        dex
        bpl     @z
        lda     #<dfcb
        ldx     #>dfcb
        ldy     #F_DELETE
        jsr     RX_BDOS
        lda     #<rfcb
        ldx     #>rfcb
        ldy     #F_RENAME
        jsr     RX_BDOS
        jsr     RX_RESTORE
        bra     @done
@erased:
        ldx     #0
@c:     lda     serased,x
        beq     @done
        phx
        ldy     #C_WRITE
        jsr     RX_BDOS
        plx
        inx
        bra     @c
@done:  lda     #M_DONE
        sta     mode
        jmp     RX_REMOVE

        .include "rsx.inc"

        .rodata
sprompt: .byte  "PUT>", 0
serased: .byte  CR, LF, "PUT ERROR: FILE ERASED.", CR, LF, 0

        .segment "RSXHDR"
PREFIX  = * - RSX_PREFIX_LEN

        .data                           ; in the file: as loaded (BSS isn't cleared)
mode:   .byte   M_IDLE
flags:  .byte   0
kind:   .byte   0
user:   .byte   0
lastout: .byte  0
bidx:   .byte   0
ch:     .byte   0
delim:  .byte   0
bp:     .word   0
bn:     .word   0
sa:     .byte   0
sx:     .byte   0
sy:     .byte   0
final:  .res    12
fcb:    .res    36
rfcb:   .res    36
dfcb:   .res    36
buf:    .res    128

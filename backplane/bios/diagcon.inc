; The diagnostics' console and their end (hw/diag.md, plan 2): included by each
; block's unit (diag21.s, diag22.s, diag23.s) in its own segment, so each block
; has its own copy and none calls another's. Nothing here is exported: each
; unit's labels are its own. Everything here needs the core's zero page and
; stack (stage 3 on); stages 0-2 and the prompt are diag23.s's macros.
;
; The output goes to the serial line (19,200 baud, as the BIOS sets it) and,
; with a revision 2 video card, to the text screen's form: PUTC writes A to
; both, the screen's cell at DG_ROW, DG_COL through the card's port (DG_SCR
; bit 6 says whether the screen takes it now). A CR is the next line on
; both. X and Y are kept by PUTC, PUTS, HEX2 and PUTDEC.

; A to the serial line and, if DG_SCR bit 6 is set, the screen.
PUTC:
                pha
                phx
                sta     ACIA_DATA
                cmp     #$0D
                bne     @char
                stz     DG_COL
                inc     DG_ROW
                bra     @tx
@char:          bit     DG_SCR
                bvc     @col
                pha
                ldx     DG_ROW
                lda     DG_COL
                asl     a
                clc
                adc     ROWLO,x
                sta     VIDEO_PADDR
                lda     ROWHI,x
                adc     #0
                sta     VIDEO_PADDR + 1
                stz     VIDEO_PADDR + 2
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                stz     VIDEO_PCTL
                pla
                sta     VIDEO_PDATA
                lda     #DG_ATTR
                sta     VIDEO_PDATA
@col:           inc     DG_COL         ; the column counts on the serial line too
@tx:            ldx     #3             ; 10 bits at 19,200 baud, as the BIOS's ECHO waits
@d1:            lda     #0
@d2:            dec     a
                bne     @d2
                dex
                bne     @d1
                plx
                pla
                rts

; The string at A/X, ended by 0.
PUTS:
                sta     DG_PTR
                stx     DG_PTR + 1
                phy
                ldy     #0
@next:          lda     (DG_PTR),y
                beq     @done
                jsr     PUTC
                iny
                bne     @next
@done:          ply
                rts

CRLF:           lda     #$0D
                jmp     PUTC

SPACE:          lda     #' '
                jmp     PUTC

; A in hex, two digits.
HEX2:
                pha
                lsr     a
                lsr     a
                lsr     a
                lsr     a
                jsr     @digit
                pla
                pha
                jsr     @digit
                pla
                rts
@digit:         and     #$0F
                cmp     #10
                bcc     @num
                adc     #'A' - '0' - 10 - 1
@num:           adc     #'0'
                jmp     PUTC

; A (0-255) in decimal, no leading zeros.
PUTDEC:
                phx
                phy
                ldy     #0             ; a digit printed yet
                ldx     #100
                jsr     @place
                ldx     #10
                jsr     @place
                ora     #'0'
                jsr     PUTC
                ply
                plx
                rts
@place:         stx     DG_T0 + 3
                ldx     #'0'
@sub:           cmp     DG_T0 + 3
                bcc     @out
                sbc     DG_T0 + 3
                inx
                bra     @sub
@out:           cpx     #'0'
                bne     @print
                cpy     #0
                beq     @none
@print:         pha
                txa
                jsr     PUTC
                pla
                ldy     #1
@none:          rts

; The screen's cursor to row A, column 0, and the screen on for what follows
; (if there is one); X kept.
ATROW:
                sta     DG_ROW
                stz     DG_COL
SCRON:          lda     DG_SCR
                and     #$80
                lsr     a              ; bit 7 (a screen) into bit 6 (write to it)
                ora     DG_SCR
                sta     DG_SCR
                rts
SCROFF:         lda     DG_SCR
                and     #$BF
                sta     DG_SCR
                rts

; A stage's line opened: A the stage, A/X... (DG_PTR) its name: "nn  NAME" and
; dots to DG_RESCOL. The lights show the stage. The result comes with
; STAGEEND, or "FAIL" at once with the stage's first DETAIL.
STAGEBEG:
                sta     DG_STAGE
                sta     CONSOLE_DISPLAY
                stz     DG_NDET
                clc
                adc     #2
                jsr     ATROW
                lda     DG_STAGE
                cmp     #10
                bcs     @two
                jsr     SPACE
                lda     DG_STAGE
@two:           jsr     PUTDEC
                jsr     SPACE
                jsr     SPACE
                lda     DG_T0
                ldx     DG_T0 + 1
                jsr     PUTS
                jsr     SPACE
@dots:          lda     DG_COL
                cmp     #DG_RESCOL - 1
                bcs     @end
                lda     #'.'
                jsr     PUTC
                bra     @dots
@end:           lda     #' '
                jmp     PUTC

; The stage's result, A a DR_*: its word at the line's end, kept in DG_RES
; and counted. If a DETAIL closed the line with FAIL already, nothing more.
STAGEEND:
                ldx     DG_STAGE
                ldy     DG_NDET
                bne     @done
                jsr     WORD
                sta     DG_RES,x
                cmp     #DR_FAIL
                bne     @done
                jsr     FAILED
@done:          rts

; The word for A (a DR_*) at the line's end, then CR; A, X kept.
WORD:
                pha
                phx
                tax
                lda     WORDHI - 1,x
                tay
                lda     WORDLO - 1,x
                phy
                plx
                jsr     PUTS
                jsr     CRLF
                plx
                pla
                rts

; A stage has failed (X): counted once, and the first failure's stage kept.
FAILED:
                pha
                lda     #DR_FAIL
                sta     DG_RES,x
                inc     DG_NFAIL
                lda     DG_FSTAGE
                bne     @had
                stx     DG_FSTAGE
@had:           pla
                rts

; A detail line under the stage's line: the first closes the stage's line with
; FAIL (and counts it). The serial line gets every one; the screen only the
; run's first failure's first, on its own row. On return the line is open
; for the caller's text, indented, with the screen on or off as said.
DETAIL:
                ldy     DG_NDET
                bne     @more
                lda     #DR_FAIL
                jsr     WORD
                ldx     DG_STAGE
                jsr     FAILED
@more:          inc     DG_NDET
                jsr     SCROFF
                lda     DG_SHOWN       ; the run's first detail: on the screen too
                bne     @serial
                inc     DG_SHOWN
                lda     #DG_ROWDET
                sta     DG_ROW
                stz     DG_COL
                jsr     SCRON
@serial:        ldx     #6
@ind:           jsr     SPACE
                dex
                bne     @ind
                rts

; A memory fault's detail: block DG_T0, offset DG_T0+1 (2 bytes), read DG_T0+3
; DG_MGOT, written DG_MWANT; kept in DG_FBLK etc. if it is the run's first: "BLOCK bb +oooo
; (ppppp): READ xx, WROTE yy, BIT n".
MEMFAULT:
                lda     DG_SHOWN
                bne     @say0
                lda     DG_T0
                sta     DG_FBLK
                lda     DG_T0 + 1
                sta     DG_FOFS
                lda     DG_T0 + 2
                sta     DG_FOFS + 1
                lda     DG_MGOT
                sta     DG_FGOT
                lda     DG_MWANT
                sta     DG_FWANT
@say0:          jsr     DETAIL
                lda     #<SBLOCK
                ldx     #>SBLOCK
                jsr     PUTS
                lda     DG_T0
                jsr     PUTDEC
                lda     #<SPLUS
                ldx     #>SPLUS
                jsr     PUTS
                lda     DG_T0 + 2
                jsr     HEX2
                lda     DG_T0 + 1
                jsr     HEX2
                lda     #<SPAREN
                ldx     #>SPAREN
                jsr     PUTS
                lda     DG_T0          ; the physical address: block x $4000 + offset
                lsr     a
                lsr     a
                jsr     HEXDIG
                lda     DG_T0
                asl     a
                asl     a
                asl     a
                asl     a
                asl     a
                asl     a
                ora     DG_T0 + 2
                jsr     HEX2
                lda     DG_T0 + 1
                jsr     HEX2
                lda     #<SREAD
                ldx     #>SREAD
                jsr     PUTS
                lda     DG_MGOT
                jsr     HEX2
                lda     #<SWROTE
                ldx     #>SWROTE
                jsr     PUTS
                lda     DG_MWANT
                jsr     HEX2
                lda     DG_MGOT
                eor     DG_MWANT
                beq     @end
                sta     DG_T0 + 3
                lda     #<SBIT
                ldx     #>SBIT
                jsr     PUTS
                ldx     #0
@bit:           lsr     DG_T0 + 3
                bcc     @nob
                lda     #' '
                jsr     PUTC
                txa
                ora     #'0'
                jsr     PUTC
@nob:           inx
                cpx     #8
                bne     @bit
@end:           jmp     CRLF

HEXDIG:         and     #$0F
                cmp     #10
                bcc     @num
                adc     #'A' - '0' - 10 - 1
@num:           adc     #'0'
                jmp     PUTC

; A key: the serial line, the USB keyboard, or the panel's CONT (as CR) and
; EXAM (as Esc). Carry set and A the key if there was one.
KEYGET:
                lda     ACIA_STATUS
                and     #$08
                beq     @usb
                lda     ACIA_DATA
                and     #$7F
                bra     @got
@usb:           lda     USB_ID
                cmp     #USB_ID_V1
                bne     @panel
                lda     USB_KEY
                bpl     @panel
                bit     USB_KEYCLR
                and     #$7F
                bra     @got
@panel:         lda     CONSOLE_ID
                cmp     #CONSOLE_ID_V1
                bne     @none
                lda     CONSOLE_KEYS   ; the keys since the last read: CONT and EXAM count
                bit     #$08
                bne     @cont
                bit     #$02
                beq     @none
                lda     #$1B
                sec
                rts
@cont:          lda     #$0D
@got:           sec
                rts
@none:          clc
                rts

; The block W2 holds next: A the block (DG_B22, DG_B21 or DG_B23), X its
; entry, if its signature and version say it is this build's (read through
; W1); carry set, and back, if not.
HANDOFF:
                pha
                sta     NB_WIN1
                ldy     #7
@sig:           lda     $4000,y
                cmp     SIG,y
                bne     @no
                dey
                bpl     @sig
                lda     $4000 + 9
                cmp     #DG_VERSION
                bne     @no
                lda     #1
                sta     NB_WIN1
                pla
                jmp     DG_GATE
@no:            lda     #1
                sta     NB_WIN1
                pla
                sec
                rts

; Stages A to DG_T0+2 not in the flash (a block erased, or another build's):
; each's line, SKIP, with why.
NOTFLASH:
                sta     DG_KEY
@stage:         lda     DG_KEY
                asl     a
                tax
                lda     STNAMES - 14,x
                sta     DG_T0
                lda     STNAMES - 13,x
                sta     DG_T0 + 1
                lda     DG_KEY
                jsr     STAGEBEG
                lda     #DR_SKIP
                jsr     STAGEEND
                jsr     SCROFF
                lda     #<SNOTFL
                ldx     #>SNOTFL
                jsr     PUTS
                inc     DG_KEY
                lda     DG_KEY
                cmp     DG_T0 + 2
                bcc     @stage
                beq     @stage
                rts

; The end of a pass (stage 14): the result, then the loop's next pass, or
; a key: CONT, RETURN or any other the boot, EXAM or Esc the monitor. What
; the run changed is put back first, as /RESET leaves it.
ENDRUN:
                lda     #14
                sta     DG_STAGE
                sta     CONSOLE_DISPLAY
                lda     #DG_ROWRES
                jsr     ATROW
                lda     #<SRESULT
                ldx     #>SRESULT
                jsr     PUTS
                lda     DG_NFAIL
                bne     @failed
                lda     #<SALLOK
                ldx     #>SALLOK
                jsr     PUTS
                lda     #$FF
                bra     @lights
@failed:        jsr     PUTDEC
                lda     #<SFAILED
                ldx     #>SFAILED
                jsr     PUTS
                lda     DG_NFAIL
                ora     #$C0
@lights:        sta     DG_T0
@pad:           lda     DG_COL
                cmp     #56
                bcs     @lamp
                jsr     SPACE
                bra     @pad
@lamp:          lda     #<SLIGHTS
                ldx     #>SLIGHTS
                jsr     PUTS
                lda     DG_T0
                jsr     HEX2
                jsr     CRLF
                lda     DG_T0
                sta     CONSOLE_DISPLAY
                lda     DG_OPT
                and     #DO_LOOP
                beq     @keys
                jsr     KEYGET         ; the loop: a key ends it after this pass
                bcs     @keys
                inc     DG_PASS
                bne     @again
                inc     DG_PASS + 1
@again:         lda     #DG_B23
                ldx     #DE_LOOP
                jmp     DG_GATE        ; block 23's signature is the one the run started from
@keys:          lda     #DG_ROWKEYS
                jsr     ATROW
                lda     #<SKEYS
                ldx     #>SKEYS
                jsr     PUTS
                jsr     CRLF
@wait:          jsr     KEYGET
                bcc     @wait
                sta     DG_KEY
                jsr     PUTBACK
                lda     DG_KEY
                cmp     #$1B
                beq     @mon
                jmp     BOOT
@mon:           jmp     MONBOOT

; What the run changed, back to the values /RESET leaves (hw/diag.md, plan
; 2): the aperture's slots, the port, the DMA controller's CONTROL, the sound
; card's 6510 stopped, the VIA (ACR 0, timer 1 a one-shot left to run out,
; its flags cleared and its interrupts off), WAIT0-3, the lights, VEC on the
; flash's top block, and W0 and W1 on blocks 0 and 1. The caller jumps to the
; BIOS (W3) at once after: W0's move takes the stack with it. W2 stays on
; this block until the BIOS maps its own.
PUTBACK:
                ldx     #6
@slot:          txa
                lsr     a
                sta     VIDEO_SLOT0,x
                stz     VIDEO_SLOT0 + 1,x
                dex
                dex
                bpl     @slot
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                stz     VIDEO_PCTL
                lda     #$70
                sta     DMA_CONTROL
                lda     SOUND_ID
                and     #$F0
                cmp     #SOUND_TYPE
                bne     @via
                lda     SOUND_CTRL
                and     #<~SOUND_RUN
                sta     SOUND_CTRL
@via:           stz     VIA_ACR        ; timer 1 one-shot; then started on a count it ends at once
                lda     #2
                sta     VIA_T1CL
                stz     VIA_T1CH
                nop
                nop
                nop
                nop
                lda     #$7F
                sta     VIA_IER
                sta     VIA_IFR
                stz     NB_WAIT0
                stz     NB_WAIT1
                stz     NB_WAIT2
                stz     NB_WAIT3
                stz     CONSOLE_DISPLAY
                stz     CONSOLE_CTRL
                lda     #DG_FLASH31
                sta     NB_VEC
                lda     #1
                sta     NB_WIN1
                ply                    ; the return, off the stack before W0 moves
                plx
                stz     NB_WIN0
                phx
                phy
                rts

; The text screen's rows' card addresses
ROWLO:
.repeat DG_ROWS, r
                .byte   <(DG_TEXT + r * DG_COLS * 2)
.endrepeat
ROWHI:
.repeat DG_ROWS, r
                .byte   >(DG_TEXT + r * DG_COLS * 2)
.endrepeat

WORDLO:         .byte   <SPASS, <SFAIL, <SSKIP
WORDHI:         .byte   >SPASS, >SFAIL, >SSKIP
SPASS:          .byte   "PASS", 0
SFAIL:          .byte   "FAIL", 0
SSKIP:          .byte   "SKIP", 0
SIG:            .byte   "OOHWDIAG"
SBLOCK:         .byte   "BLOCK ", 0
SPLUS:          .byte   " +", 0
SPAREN:         .byte   " (", 0
SREAD:          .byte   "): READ ", 0
SWROTE:         .byte   ", WROTE ", 0
SBIT:           .byte   ", BITS", 0
SNOTFL:         .byte   "      NOT IN THE FLASH: ITS BLOCK IS ERASED OR FROM ANOTHER BUILD", $0D, 0
SRESULT:        .byte   "RESULT: ", 0
SALLOK:         .byte   "ALL PASSED", 0
SFAILED:        .byte   " FAILED", 0
SLIGHTS:        .byte   "LIGHTS ", 0
SKEYS:          .byte   "CONT OR RETURN: BOOT        EXAM OR ESC: MONITOR", 0

; The names of stages 7-13, for the lines the blocks after 23 write
STNAMES:        .word   SN7, SN8, SN9, SN10, SN11, SN12, SN13
SN7:            .byte   "NORTH BRIDGE AND CLOCK", 0
SN8:            .byte   "DMA", 0
SN9:            .byte   "VIDEO CARD", 0
SN10:           .byte   "SOUND CARD", 0
SN11:           .byte   "STORAGE, READ ONLY", 0
SN12:           .byte   "USB, SERIAL AND PANEL", 0
SN13:           .byte   "INTERACTIVE", 0

; SERTEST: the serial line's receive buffer (hw/oodos.md, "OODOS: a receive
; buffer for the serial line"), for test_oodos_serial. The command tail is a
; case letter and two hex digits, n: busy for n units of about 46 ms
; (329,728 cycles, interrupts on, no BDOS call), so that what comes on the
; line meanwhile has only the ring to keep it. CONIN: is CRT alone while it
; runs (function 49 on SCB $22, put back before it ends), so the BDOS's
; look at the console before each character it prints takes nothing of
; AUX:'s.
;
;   R   busy, then AUX:'s bytes read by function 7 and 3 until 7 has said 0
;       for about 0.3 s: N nnnn, the count, and A aaaa B bbbb, their
;       Fletcher sums (A the bytes' sum, B the sum of A after each, both
;       modulo 65,536); S7 xx, function 7 after
;   P   R's, then busy and read again: N2 nnnn A2 aaaa B2 bbbb
;   S   busy, then function 85 (O_SERIAL): INFO aa xx, INFOB pp ss (SR_INFO,
;       with no block and with one); ST aa xx and STB, SR_STATUS's +2-+6
;       (waiting, room, lost low and high, flags); one byte by function 3,
;       GOT cc; ST2 and STB2 after it; BAD1 aa xx (port 1), BAD3 aa xx
;       (operation 3); FL aa xx (SR_FLUSH), ST3 and STB3; then R's reading
;   Z   busy, then the warm boot with nothing read
;   Q   S7 xx: function 7 at once, then the warm boot
;   C   CMD cc: the 6551's command register as the program finds it
;
; A fourth character, for R, P and Z, opens an event area at AREA ($2000,
; W0's block 0) first: 1 one of layout 1 (16 bytes), 2 one of layout 2 (256),
; kept open to the end (FT_CLOSE then); X one opened and closed at once, its
; 256 bytes then filled with $5A, so the test sees that nothing writes there.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "backplane.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .zeropage
units:  .res    1                      ; the tail's n
toff:   .res    1                      ; the letter's offset in the tail: 1, or 2 after a blank
cnt:    .res    2
suma:   .res    2
sumb:   .res    2
idle:   .res    2

AREA    = $2000

        .code
start:  ldx     #1
        lda     BP_DMA + 1
        cmp     #' '
        bne     :+
        inx
:       stx     toff
        lda     BP_DMA + 1,x           ; n, two hex digits after the letter
        jsr     hexdig
        asl     a
        asl     a
        asl     a
        asl     a
        sta     units
        ldx     toff
        lda     BP_DMA + 2,x
        jsr     hexdig
        ora     units
        sta     units
        ldx     toff
        lda     BP_DMA,x
        cmp     #'Q'
        bne     :+
        jsr     s7
        jmp     WBOOT
:       cmp     #'C'
        bne     :+
        jmp     case_c
:       pha
        jsr     evopen
        jsr     crtonly
        jsr     busy
        pla
        cmp     #'Z'
        beq     done
        pha
        cmp     #'S'
        bne     :+
        jsr     case_s
:       jsr     readall
        lda     #<sn
        ldx     #>sn
        jsr     report
        pla
        cmp     #'P'
        bne     last
        jsr     busy
        jsr     readall
        lda     #<sn2
        ldx     #>sn2
        jsr     report
last:   jsr     s7
done:   jsr     conback
        ldx     toff
        lda     BP_DMA + 3,x
        cmp     #'1'
        beq     :+
        cmp     #'2'
        bne     :++
:       lda     #FT_CLOSE
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
:       jmp     WBOOT

; The event area the fourth character asks for.
evopen: ldx     toff
        lda     BP_DMA + 3,x
        ldx     #16
        cmp     #'1'
        beq     @open
        ldx     #0
        cmp     #'2'
        beq     @open
        cmp     #'X'
        bne     @none
        jsr     @open
        lda     #FT_CLOSE
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        ldx     #0
        lda     #$5A
:       sta     AREA,x
        inx
        bne     :-
@none:  rts
@open:  stx     evblk + 3
        lda     #<evblk
        ldx     #>evblk
        ldy     #O_FRAME
        jmp     BDOS

; C: the 6551's command register, CMD cc.
case_c: lda     #<scmd
        ldx     #>scmd
        jsr     PUTS
        lda     ACIA_CMD
        jsr     HEX2
        jsr     CRLF
        jmp     WBOOT

; S: function 85 after busy (n units): INFO aa xx and INFOB pp ss (SR_INFO
; with no block and with one); ST aa xx and STB, SR_STATUS's +2-+6 (waiting,
; room, lost low and high, flags); one byte read by 3, GOT cc, and ST2 aa xx
; STB2 the same after it; BAD1 aa xx (port 1), BAD3 aa xx (operation 3); FL
; aa xx (SR_FLUSH), ST3 aa xx STB3; then R's reading: N A B, S7.
case_s: lda     #SR_INFO
        ldx     #0
        ldy     #O_SERIAL
        jsr     BDOS
        phx
        pha
        lda     #<sinfo
        ldx     #>sinfo
        jsr     PUTS
        pla
        plx
        jsr     pair
        lda     #SR_INFO
        sta     srblk
        lda     #<srblk
        ldx     #>srblk
        ldy     #O_SERIAL
        jsr     BDOS
        lda     #<sinfob
        ldx     #>sinfob
        jsr     PUTS
        lda     srblk + 1
        jsr     HEX2
        lda     srblk + 2
        jsr     HEX2
        jsr     CRLF
        lda     #<sst
        ldx     #>sst
        jsr     status
        ldy     #A_READ
        jsr     BDOS
        pha
        lda     #<sgot
        ldx     #>sgot
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     CRLF
        lda     #<sst2
        ldx     #>sst2
        jsr     status
        lda     #SR_STATUS              ; port 1: none
        sta     srblk
        lda     #1
        sta     srblk + 1
        lda     #<srblk
        ldx     #>srblk
        ldy     #O_SERIAL
        jsr     BDOS
        phx
        pha
        lda     #<sbad1
        ldx     #>sbad1
        jsr     PUTS
        pla
        plx
        jsr     pair
        lda     #SR_OPS                 ; an operation past the last
        ldx     #0
        ldy     #O_SERIAL
        jsr     BDOS
        phx
        pha
        lda     #<sbad3
        ldx     #>sbad3
        jsr     PUTS
        pla
        plx
        jsr     pair
        lda     #SR_FLUSH
        sta     srblk
        stz     srblk + 1
        lda     #<srblk
        ldx     #>srblk
        ldy     #O_SERIAL
        jsr     BDOS
        phx
        pha
        lda     #<sfl
        ldx     #>sfl
        jsr     PUTS
        pla
        plx
        jsr     pair
        lda     #<sst3
        ldx     #>sst3
        jsr     status
        rts

; "label aa xx" for SR_STATUS on port 0 by a block, then "labelB" and its +2-+6.
status: sta     PTR
        stx     PTR + 1
        lda     #SR_STATUS
        sta     srblk
        stz     srblk + 1
        lda     #<srblk
        ldx     #>srblk
        ldy     #O_SERIAL
        jsr     BDOS
        phx
        pha
        lda     PTR
        ldx     PTR + 1
        jsr     PUTS
        pla
        plx
        jsr     pair
        jsr     nextl                  ; its second label
        ldx     #5
        ; fall through
; X bytes of srblk from +2 as hex, CR LF.
srhex:  lda     #<(srblk + 2)
        sta     PTR
        lda     #>(srblk + 2)
        sta     PTR + 1
        jsr     HEXN
        jmp     CRLF

; A and X as "aa xx", CR LF.
pair:   phx
        jsr     HEX2
        jsr     SPACE
        pla
        jsr     HEX2
        jmp     CRLF

; '0'-'9', 'A'-'F' to 0-15.
hexdig: sec
        sbc     #'0'
        cmp     #10
        bcc     :+
        sbc     #'A' - '0' - 10
:       and     #$0F
        rts

; CONIN: (SCB $22) saved and set to CRT alone; and put back.
crtonly:
        lda     #0                     ; get
        sta     scbblk + 1
        lda     #<scbblk
        ldx     #>scbblk
        ldy     #S_SCB
        jsr     BDOS
        sta     civec
        stx     civec + 1
        lda     #$FE                   ; set the word
        sta     scbblk + 1
        stz     scbblk + 2
        lda     #$80                   ; device 0, CRT
        sta     scbblk + 3
        bra     scbset
conback:
        lda     #$FE
        sta     scbblk + 1
        lda     civec
        sta     scbblk + 2
        lda     civec + 1
        sta     scbblk + 3
scbset: lda     #<scbblk
        ldx     #>scbblk
        ldy     #S_SCB
        jmp     BDOS

; n units busy: 256 x 256 x 5 cycles and a little each.
busy:   ldx     units
        beq     @done
@unit:  lda     #0
@mid:   ldy     #0
@in:    dey
        bne     @in
        dec     a
        bne     @mid
        dex
        bne     @unit
@done:  rts

; AUX:'s bytes until function 7 has said 0 4,096 times running (each call a
; few hundred cycles: about 0.3 s), counted and summed.
readall:
        stz     cnt
        stz     cnt + 1
        stz     suma
        stz     suma + 1
        stz     sumb
        stz     sumb + 1
@reset: stz     idle
        lda     #$10
        sta     idle + 1
@poll:  ldy     #A_STATIN
        jsr     BDOS
        cmp     #0
        bne     @byte
        lda     idle
        bne     @dec
        dec     idle + 1
        beq     @end
@dec:   dec     idle
        bra     @poll
@byte:  ldy     #A_READ
        jsr     BDOS
        clc
        adc     suma
        sta     suma
        bcc     :+
        inc     suma + 1
:       clc
        lda     sumb
        adc     suma
        sta     sumb
        lda     sumb + 1
        adc     suma + 1
        sta     sumb + 1
        inc     cnt
        bne     @reset
        inc     cnt + 1
        bra     @reset
@end:   rts

; "N nnnn A aaaa B bbbb" with the label at A/X (its three '$'-ended parts).
report: sta     PTR
        stx     PTR + 1
        jsr     PUTS
        lda     cnt
        ldx     cnt + 1
        jsr     HEX4
        jsr     nextl
        lda     suma
        ldx     suma + 1
        jsr     HEX4
        jsr     nextl
        lda     sumb
        ldx     sumb + 1
        jsr     HEX4
        jmp     CRLF
nextl:  ldy     #0                     ; PTR past its '$', and that string printed
:       lda     (PTR),y
        iny
        cmp     #'$'
        bne     :-
        tya
        clc
        adc     PTR
        sta     PTR
        bcc     :+
        inc     PTR + 1
:       lda     PTR
        ldx     PTR + 1
        jmp     PUTS

; S7 xx: function 7.
s7:     lda     #<ss7
        ldx     #>ss7
        jsr     PUTS
        ldy     #A_STATIN
        jsr     BDOS
        jsr     HEX2
        jmp     CRLF

        .rodata
sn:     .byte   "N $", " A $", " B $"
sn2:    .byte   "N2 $", " A2 $", " B2 $"
ss7:    .byte   "S7 $"
scmd:   .byte   "CMD $"
sinfo:  .byte   "INFO $"
sinfob: .byte   "INFOB $"
sst:    .byte   "ST $", "STB $"
sst2:   .byte   "ST2 $", "STB2 $"
sst3:   .byte   "ST3 $", "STB3 $"
sgot:   .byte   "GOT $"
sbad1:  .byte   "BAD1 $"
sbad3:  .byte   "BAD3 $"
sfl:    .byte   "FL $"

        .data
scbblk: .byte   $22, 0, 0, 0           ; function 49: CONIN:'s vector
srblk:  .res    7                      ; function 85's block
evblk:  .byte   FT_OPEN
        .word   AREA
        .byte   16                     ; the size: 16, or 0 (256)

        .bss
civec:  .res    2

        .include "print.inc"

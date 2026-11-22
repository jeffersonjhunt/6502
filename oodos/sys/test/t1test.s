; T1TEST: the VIA's timer 1 as a program's, polled (OODOS 0.16, FT_INFO's
; FC_T1; hw/future/north-bridge-rev3.md, "The VIA's timer 1, given to
; programs", case O5), for test_oodos_clock.cpp. A line a result:
;
;   ONE nn    64 one-shots of 1,000 cycles (ACR's bits 7-6 %00), each flag
;             (IFR bit 6) polled for and cleared by reading T1CL: the flags
;             seen, $40
;   FREE nnnn free-running on PB7 (bits 7-6 %11), the latch $FFFE (65,536
;             cycles a period), for 10,000 ms by FT_COUNT: the flags seen
;   MS mmmm   the milliseconds FT_COUNT counted meanwhile
;
; then the warm boot, which puts ACR to 0 and every VIA interrupt off. The
; rest of ACR is kept as read, and IER is never written: it stays OODOS's.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "backplane.inc"

        OODOS_HEADER
        .include "printzp.inc"

PERIOD  = $FFFE                         ; free-running: the latch + 2 cycles a period
RUN_MS  = 10000

        .zeropage
n:      .res    2                       ; flags seen
ms0:    .res    2                       ; FT_COUNT's low word at the start
ms1:    .res    2                       ;   and now

        .code
start:  lda     VIA_ACR                 ; one-shot, PB7 off; the rest of ACR as read
        and     #$3F
        sta     VIA_ACR
        stz     n
        ldx     #64
@one:   lda     #<1000
        sta     VIA_T1CL                ; (the latch's low byte)
        lda     #>1000
        sta     VIA_T1CH                ; started, its flag cleared
@w1:    lda     VIA_IFR
        and     #VIA_IRQ_T1
        beq     @w1
        lda     VIA_T1CL                ; the flag cleared
        inc     n
        dex
        bne     @one
        lda     #<sone
        ldx     #>sone
        jsr     PUTS
        lda     n
        jsr     HEX2
        jsr     CRLF
        ; free-running, on PB7
        lda     VIA_ACR
        ora     #$C0
        sta     VIA_ACR
        stz     n
        stz     n + 1
        jsr     count
        sta     ms0
        stx     ms0 + 1
        lda     #<(PERIOD - 2)
        sta     VIA_T1CL
        lda     #>(PERIOD - 2)
        sta     VIA_T1CH                ; started: it reloads from the latch at each expiry
@w2:    lda     VIA_IFR
        and     #VIA_IRQ_T1
        beq     @w2
        lda     VIA_T1CL                ; the flag cleared
        inc     n
        bne     @t
        inc     n + 1
@t:     jsr     count                   ; ms1 = now - ms0
        sec
        sbc     ms0
        sta     ms1
        txa
        sbc     ms0 + 1
        sta     ms1 + 1
        cmp     #>RUN_MS
        bcc     @w2
        bne     @end
        lda     ms1
        cmp     #<RUN_MS
        bcc     @w2
@end:   lda     #<sfree
        ldx     #>sfree
        jsr     PUTS
        lda     n
        ldx     n + 1
        jsr     HEX4
        jsr     CRLF
        lda     #<sms
        ldx     #>sms
        jsr     PUTS
        lda     ms1
        ldx     ms1 + 1
        jsr     HEX4
        jsr     CRLF
        jmp     WBOOT

; FT_COUNT's low word in A (low) and X.
count:  lda     #FT_COUNT
        ldx     #0
        ldy     #O_FRAME
        jmp     BDOS

        .rodata
sone:   .byte   "ONE $"
sfree:  .byte   "FREE $"
sms:    .byte   "MS $"

        .include "print.inc"

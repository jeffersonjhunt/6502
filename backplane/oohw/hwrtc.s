; HW_RTC (oohw.inc): the DS3231's status as the north bridge keeps it, and
; its temperature, through RTCREG and RTCDATA (hw/design.md, "Real-time
; clock"): writing RTCREG starts the bridge's I2C read of that register,
; BUSY on until RTCDATA has it. Each wait gives up after about 0.1 s.

        .setcpu "65C02"
        .include "backplane.inc"
        .include "oohw.inc"

        .code
HW_RTC: jsr     idle
        bcs     @out
        lda     NB_RTCCTL
        sta     HW_RTCST
        lda     #$11                    ; the temperature: whole degrees, signed
        jsr     reg
        bcs     @out
        sta     HW_RTCST + 1
        lda     #$12                    ; and its quarters, bits 7-6
        jsr     reg
        bcs     @out
        sta     HW_RTCST + 2
@out:   rts

; DS3231 register A, read: A; carry set if BUSY stayed on.
reg:    sta     NB_RTCREG
        jsr     idle
        bcs     @out
        lda     NB_RTCDATA
@out:   rts

; BUSY clear: carry clear; still on after about 0.1 s (256 x 256 x 11 cycles): set.
idle:   ldx     #0
        ldy     #0
@wait:  lda     NB_RTCCTL
        and     #NB_RTC_BUSY
        beq     @ok
        dex
        bne     @wait
        dey
        bne     @wait
        sec
        rts
@ok:    clc
        rts

        .bss
HW_RTCST: .res  3

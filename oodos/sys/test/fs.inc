; The BDOS's test programs' shared code (phase 3a): an FCB from a name, the
; results printed, records of a known pattern. Included once, at the end,
; after print.inc; its zero page and macros are fszp.inc's. Through the BDOS
; only, so they keep the rules for programs.
;
;   RES                 A and X as "aa xx", then CR LF (RA, RX keep them)
;   SETFCB              the FCB at FP: drive TDRV + 1, the name at NP
;   FILL / CHECK        BUF = record RN's pattern / carry set if it isn't:
;                       byte j is (RN's low byte + j) ^ RN's high byte ^ $5A
;   ZEROS               carry set if BUF isn't all 0
;   HEX6                the 3 bytes at A/X, high first
;   YESNO               Y for carry clear, N set

        .code
; A and X as "aa xx", CR LF; RA and RX keep them.
RES:    sta     RA
        stx     RX
        jsr     HEX2
        jsr     SPACE
        lda     RX
        jsr     HEX2
        jsr     CRLF
        lda     RA
        ldx     RX
        rts

; The FCB at FP: TDRV + 1 as its drive (TDRV $FF: 0, the current one), the
; 11 bytes at NP, then 0s to its 36th byte.
SETFCB: ldy     #0
        lda     TDRV
        inc     a
        sta     (FP),y
@name:  lda     (NP),y
        iny
        sta     (FP),y
        cpy     #11
        bne     @name
        lda     #0
@zero:  iny
        sta     (FP),y
        cpy     #35
        bne     @zero
        rts

; BUF = record RN's pattern.
FILL:   ldy     #0
@byte:  tya
        clc
        adc     RN
        eor     RN+1
        eor     #$5A
        sta     BUF,y
        iny
        bpl     @byte
        rts

; Carry set if BUF isn't record RN's pattern.
CHECK:  ldy     #0
@byte:  tya
        clc
        adc     RN
        eor     RN+1
        eor     #$5A
        cmp     BUF,y
        bne     @bad
        iny
        bpl     @byte
        clc
        rts
@bad:   sec
        rts

; Carry set if BUF isn't all 0.
ZEROS:  ldy     #0
@byte:  lda     BUF,y
        bne     @bad
        iny
        bpl     @byte
        clc
        rts
@bad:   sec
        rts

; The 3 bytes at A/X, high first, as hex.
HEX6:   sta     PTR
        stx     PTR+1
        ldy     #2
@byte:  lda     (PTR),y
        phy
        jsr     HEX2
        ply
        dey
        bpl     @byte
        rts

; "Y" if carry clear, "N" if set; then CR LF.
YESNO:  lda     #'Y'
        bcc     @say
        lda     #'N'
@say:   jsr     PUTC
        jmp     CRLF

        .bss
BUF:    .res    128                     ; the DMA buffer

; STAMPTEST: date stamps (hw/oodos.md, "The BDOS, phase 3") on drive D, the
; command tail's first letter, for test_oodos_sys.cpp, which sets the clock
; while it waits and reads the SFCBs itself. "aa xx" is A and X.
;
;   MAKE aa xx            D:STAMP.DAT: its creation stamp. MADE? waits
;   WRITE aa / CLOSE aa xx  a record, then the close: its update. CLOSED?
;   EMPTY aa xx           D:EMPTY.DAT made and closed unwritten: no update
;   MODE? waits (the test may change the label's mode), then
;   RESET aa xx           37 of D:, so the label is read again
;   OPEN aa xx            STAMP.DAT: an access stamp if the mode asks.
;                         OPENED? waits
;   STAMPTEST DONE

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

        .code
start:  CALLF   F_DMAOFF, BUF
        lda     BP_DMA+2
        sec
        sbc     #'A'
        sta     TDRV
        NAME    fcb, "STAMP   DAT"
        TRY     "MAKE ", F_MAKE, fcb
        KEYWAIT "MADE?"
        stz     RN
        stz     RN+1
        jsr     FILL
        SAY     "WRITE "
        CALLF   F_WRITE, fcb
        jsr     HEX2
        jsr     CRLF
        TRY     "CLOSE ", F_CLOSE, fcb
        KEYWAIT "CLOSED?"
        NAME    fcb2, "EMPTY   DAT"
        CALLF   F_MAKE, fcb2
        TRY     "EMPTY ", F_CLOSE, fcb2
        KEYWAIT "MODE?"
        lda     #1                      ; D:'s bit
        sta     vec
        stz     vec+1
        ldx     TDRV
@bit:   dex
        bmi     @reset
        asl     vec
        rol     vec+1
        bra     @bit
@reset: SAY     "RESET "
        lda     vec
        ldx     vec+1
        ldy     #DRV_RESET
        jsr     BDOS
        jsr     RES
        NAME    fcb, "STAMP   DAT"
        TRY     "OPEN ", F_OPEN, fcb
        KEYWAIT "OPENED?"
        SAY     "STAMPTEST DONE"
        jmp     CRLF

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36
fcb2:   .res    36
vec:    .res    2

; LOGINTEST: a drive's login, on the first select after a reset (hw/oodos.md,
; "The BDOS, phase 3"), and the warm boot's write-back, for
; test_oodos_sys.cpp, which counts the BIOS's READs between the labels and
; changes M:'s directory behind the BDOS's back. "aa xx" is A and X.
;
;   (a1-a2) function 46 of A:: its login, the directory read once
;   (a2-a3) again: nothing read
;   (m1-m2) 46 of M:; ADDED? waits while the test puts LATE.TXT in M:'s
;   directory
;   BEFORE aa xx          Open LATE.TXT: FF, the BDOS has M:'s directory
;   (m3-m4) RESET aa xx   37 of M:, then AFTER aa xx: Open finds it now
;   UNCLOSED? waits: 4 records written to M:W.DAT, not closed, still in
;   their buffers; then the program ends, and the warm boot writes them

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

DRV_M   = 12

        .code
start:  CALLF   F_DMAOFF, BUF
a1:     lda     #0
        ldy     #DRV_SPACE
        jsr     BDOS
a2:     lda     #0
        ldy     #DRV_SPACE
        jsr     BDOS
a3:
m1:     lda     #DRV_M
        ldy     #DRV_SPACE
        jsr     BDOS
m2:     KEYWAIT "ADDED?"
        lda     #DRV_M
        sta     TDRV
        NAME    fcb, "LATE    TXT"
        TRY     "BEFORE ", F_OPEN, fcb
        SAY     "RESET "
m3:     lda     #<(1 << DRV_M)
        ldx     #>(1 << DRV_M)
        ldy     #DRV_RESET
        jsr     BDOS
        jsr     RES
        NAME    fcb, "LATE    TXT"
        CALLF   F_OPEN, fcb
m4:     sta     RA
        stx     RX
        SAY     "AFTER "
        lda     RA
        ldx     RX
        jsr     RES
        NAME    fcb, "W       DAT"
        CALLF   F_MAKE, fcb
        stz     RN
        stz     RN+1
@write: jsr     FILL
        CALLF   F_WRITE, fcb
        inc     RN
        lda     RN
        cmp     #4
        bne     @write
        KEYWAIT "UNCLOSED?"
        rts                             ; not closed

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36

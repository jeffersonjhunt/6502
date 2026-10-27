; ERRTEST: the BDOS's errors as the error mode says (hw/oodos.md, "The
; BDOS, phase 3"), a case by the command tail's first letter, for
; test_oodos_sys.cpp. "aa xx" is A and X as the BDOS returned them. Each
; case ends in the default mode with an error that ends the program, so
; NOT REACHED is never printed.
;
;   B   B: a read-only unit: RO aa xx (mode $FF: FF 02); then mode $FE, the
;       message and BACK aa xx; then the default: the message, the end
;   M   on M:: EXISTS aa xx (FF 08), WILD aa xx (FF 09); mode $FE: the
;       message (File Exists), BACK aa xx; then a write to a read-only file
;       in the default mode: File R/O, the end
;   P   function 14 of P: in the default mode: Invalid Drive, the end
;   I   A: logged in, then EJECT? waits while the test takes the card out:
;       IO aa xx (FF 01, Disk I/O, opening README.TXT); then the default
;   W   B: writable at its login, a record written to B:WP.TXT, then
;       REMOUNT? waits while the test makes the unit read-only: the close's
;       write-back FF 02 (WPCLOSE), and B: read-only since (WPROVEC); then
;       a make there in the default mode

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

        .code
start:  CALLF   F_DMAOFF, BUF
        lda     BP_DMA+2                ; the case
        cmp     #'B'
        beq     caseb
        cmp     #'M'
        bne     :+
        jmp     casem
:       cmp     #'P'
        bne     :+
        jmp     casep
:       cmp     #'I'
        bne     :+
        jmp     casei
:       cmp     #'W'
        bne     :+
        jmp     casew
:       SAY     "NO CASE"
        jmp     CRLF

mode:   ldy     #F_ERRMODE
        jmp     BDOS

caseb:  lda     #1                      ; B:
        sta     TDRV
        lda     #$FF
        jsr     mode
        NAME    fcb, "X       TXT"
        TRY     "RO ", F_MAKE, fcb
        lda     #$FE
        jsr     mode
        NAME    fcb, "X       TXT"
        CALLF   F_MAKE, fcb
        jsr     back
        lda     #0
        jsr     mode
        NAME    fcb, "X       TXT"
        CALLF   F_MAKE, fcb
        jmp     notreached

casem:  lda     #12                     ; M:
        sta     TDRV
        lda     #$FF
        jsr     mode
        NAME    fcb, "ROF     TXT"
        CALLF   F_MAKE, fcb
        CALLF   F_WRITE, fcb
        CALLF   F_CLOSE, fcb
        NAME    fcb, "ROF     TXT"
        TRY     "EXISTS ", F_MAKE, fcb
        NAME    fcb, "R?F     TXT"
        TRY     "WILD ", F_OPEN, fcb
        lda     #$FE
        jsr     mode
        NAME    fcb, "ROF     TXT"
        CALLF   F_MAKE, fcb
        jsr     back
        NAME    fcb, "ROF     TXT"
        lda     fcb+9
        ora     #$80                    ; t1': read-only
        sta     fcb+9
        CALLF   F_ATTRIB, fcb
        lda     #0
        jsr     mode
        NAME    fcb, "ROF     TXT"
        CALLF   F_OPEN, fcb
        CALLF   F_WRITE, fcb
        jmp     notreached

casep:  lda     #0
        jsr     mode
        lda     #15
        ldy     #DRV_SET
        jsr     BDOS
        jmp     notreached

casei:  lda     #0                      ; A: logged in
        ldy     #DRV_SPACE
        jsr     BDOS
        KEYWAIT "EJECT?"
        lda     #$FF
        jsr     mode
        lda     #0
        sta     TDRV
        NAME    fcb, "README  TXT"
        TRY     "IO ", F_OPEN, fcb
        lda     #0
        jsr     mode
        NAME    fcb, "README  TXT"
        CALLF   F_OPEN, fcb
        jmp     notreached

casew:  lda     #1
        sta     TDRV
        lda     #$FF
        jsr     mode
        NAME    fcb, "WP      TXT"
        CALLF   F_MAKE, fcb
        CALLF   F_WRITE, fcb
        KEYWAIT "REMOUNT?"
        TRY     "WPCLOSE ", F_CLOSE, fcb
        TRY     "WPROVEC ", DRV_ROVEC, 0
        lda     #0
        jsr     mode
        NAME    fcb, "WP2     TXT"
        CALLF   F_MAKE, fcb
        ; fall through

; "BACK aa xx": A and X kept past the SAY.
back:   sta     RA
        stx     RX
        SAY     "BACK "
        lda     RA
        ldx     RX
        jmp     RES

notreached:
        SAY     "NOT REACHED"
        jmp     CRLF

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36

; DIAGWB: function 75 with A = 2 (O_ROM, the ROM's diagnostics; interface
; 10; hw/diag.md's plan 2, D3c) from a program, for test_oodos_utils_rom.
; 0:DIAGWB.TXT on A: is made with a record of "BEFORE" and closed; then
; opened again and its record 0 written again with "AFTER", and not closed,
; so the new record is in a changed buffer only, which 75 must write back
; before it leaves. Then 75 with A = 2 and X = 0 (a quick run). Without the
; diagnostics in the flash 75 comes back: A and X are printed.
;
;   step aa xx      a step that failed: MAKE, WRITE, CLOSE, OPEN, REWRITE
;   LEAVING         the file written, 75 next
;   aa xx           75's A and X, if it came back

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

        .code
start:  lda     #$FF                    ; errors come back, said here
        ldy     #F_ERRMODE
        jsr     BDOS
        CALLF   F_DMAOFF, BUF
        stz     TDRV                    ; A:
        lda     #0
        ldy     #F_USERNUM
        jsr     BDOS
        NAME    fcb, "DIAGWB  TXT"
        CALLF   F_DELETE, fcb
        NAME    fcb, "DIAGWB  TXT"
        CALLF   F_MAKE, fcb
        cmp     #$FF
        bne     made
        jsr     keep
        SAY     "MAKE "
        jmp     failed
made:   lda     #<before
        ldx     #>before
        jsr     record
        CALLF   F_WRITE, fcb
        cmp     #0
        beq     wrote
        jsr     keep
        SAY     "WRITE "
        jmp     failed
wrote:  CALLF   F_CLOSE, fcb
        cmp     #$FF
        bne     closed
        jsr     keep
        SAY     "CLOSE "
        jmp     failed
closed: NAME    fcb, "DIAGWB  TXT"
        CALLF   F_OPEN, fcb
        cmp     #$FF
        bne     opened
        jsr     keep
        SAY     "OPEN "
        jmp     failed
opened: lda     #<after
        ldx     #>after
        jsr     record
        CALLF   F_WRITE, fcb            ; record 0 again: in a changed buffer, the file left open
        cmp     #0
        beq     leave
        jsr     keep
        SAY     "REWRITE "
        jmp     failed
leave:  SAY     "LEAVING"
        jsr     CRLF
        lda     #2                      ; the diagnostics
        ldx     #0                      ; quick, no questions
        ldy     #O_ROM
        jsr     BDOS
        jsr     RES                     ; back: none in the flash
        jmp     WBOOT

failed: lda     RA                      ; the step's A and X, kept by keep
        ldx     RX
        jsr     RES
        jmp     WBOOT

keep:   sta     RA
        stx     RX
        rts

; BUF = the '$'-ended text at A/X, CR LF, then ^Z to the record's end.
record: sta     NP
        stx     NP+1
        ldy     #0
@text:  lda     (NP),y
        cmp     #'$'
        beq     @end
        sta     BUF,y
        iny
        bra     @text
@end:   lda     #13
        sta     BUF,y
        iny
        lda     #10
        sta     BUF,y
        iny
        lda     #$1A
@pad:   sta     BUF,y
        iny
        bpl     @pad
        rts

        .include "print.inc"
        .include "fs.inc"

        .rodata
before: .byte   "BEFORE$"
after:  .byte   "AFTER$"

        .bss
fcb:    .res    36

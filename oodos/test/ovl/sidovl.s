; SIDOVL: the caller in test_oodos_playsid's overlay case (the Programmer's
; Guide, chapter 8, "Linking the library into an overlay"). It links OOLIB's .sid zero page,
; SID_ZP, though it calls nothing of the library itself: its overlay,
; SIDPLUG.PRL, is linked with SID_ZP's address from this program's map
; (tools/libzp.py names), and so has no zero page of its own.
;
; SIDOVL d:FILE.PRL: the overlay loaded by function 59 at $8000; then
;
;   L=xxxx            59's A and X (0000: loaded), and if it is, "R"
;   (a key)           the test has put a tune at TUNE, its length at TLEN
;   P=aa              the overlay's play (SID_PARSE, SID_PLAN for the
;                     file's start song, SID_PLAY): 00, or the error
;   (a key)
;   S                 the overlay's stop (SID_STOP), then the warm boot

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .importzp SID_ZP

OVL             = $8000                 ; the overlay: +0 play (A/X the tune), +3 stop
TLEN            = $1FFE                 ; the tune's length, then its bytes (the test's)
TUNE            = $2000

        .code
start:  lda     #SID_ZP                 ; named, so the library's module for it is linked
        lda     #<BP_FCB
        ldx     #>BP_FCB
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        beq     @fail
        lda     #<OVL
        sta     BP_FCB + 33
        lda     #>OVL
        sta     BP_FCB + 34
        stz     BP_FCB + 35
        lda     #<BP_FCB
        ldx     #>BP_FCB
        ldy     #P_LOAD
        jsr     BDOS
        sta     ra
        stx     rx
        lda     #<sl
        ldx     #>sl
        jsr     PUTS
        lda     ra
        jsr     HEX2
        lda     rx
        jsr     HEX2
        jsr     CRLF
        lda     ra
        ora     rx
        bne     @done
        lda     #'R'
        jsr     PUTC
        jsr     CRLF
        ldy     #C_READ
        jsr     BDOS
        lda     #<sp
        ldx     #>sp
        jsr     PUTS
        lda     #<TUNE
        ldx     #>TUNE
        jsr     ovlplay
        jsr     HEX2
        jsr     CRLF
        ldy     #C_READ
        jsr     BDOS
        jsr     ovlstop
        lda     #'S'
        jsr     PUTC
        jsr     CRLF
@done:  jmp     WBOOT
@fail:  lda     #<snof
        ldx     #>snof
        jsr     PUTS
        jmp     CRLF

ovlplay:jmp     (vplay)                 ; the overlay's, through vectors (rule 4: a JSR
ovlstop:jmp     (vstop)                 ; stays in the program)

        .include "print.inc"

        .rodata
snof:   .byte   "NO FILE$"
sl:     .byte   "L=$"
sp:     .byte   "P=$"

        .data
vplay:  .word   OVL
vstop:  .word   OVL + 3

        .bss
ra:     .res    1
rx:     .res    1

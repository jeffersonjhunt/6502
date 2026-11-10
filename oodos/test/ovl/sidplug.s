; SIDPLUG: test_oodos_playsid's overlay, a .PRL for function 59 linked with
; OOLIB's .sid (the Programmer's Guide, chapter 8, "Linking the library into
; an overlay"). Its zero page is its caller's, SIDOVL's: SID_ZP's address from SIDOVL's map
; (tools/libzp.py names), so its own zero page size is 0. SIDPLUGZ is the
; same overlay linked without it, which pulls the library's zpsid and is
; refused.
;
;   +0  play: A/X the tune, its length at $1FFE; SID_PARSE, SID_PLAN (the
;       file's start song) and SID_PLAY. A = 0, or the failing call's error
;   +3  stop: SID_STOP

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        OODOS_HEADER

TLEN            = $1FFE

        .code
        jmp     play
        jmp     SID_STOP
play:   pha
        lda     TLEN
        sta     SID_LEN
        lda     TLEN + 1
        sta     SID_LEN + 1
        pla
        jsr     SID_PARSE
        bcs     @out
        lda     #0
        tax
        jsr     SID_PLAN
        bcs     @out
        jsr     SID_PLAY
        bcs     @out
        lda     #0
@out:   rts

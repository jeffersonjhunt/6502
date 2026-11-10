; The C runtime's start for an OODOS .COM (Plan C1; the Programmer's Guide,
; appendix F, "A program in C"). cc65's compiled modules import __STARTUP__,
; so ld65 takes this module from oodosc.lib, ahead of none.lib's own.
;
; The C stack runs down from the top of the TPA (BP_TOP: the BDOS's, or the
; top RSX's, entry). zerobss clears BSS, initlib runs the constructors (the
; heap's floor, argc and argv), and callmain calls main(argc, argv). Its
; return, or exit(n), comes to _exit: the destructors run, a status other
; than 0 is set as function 108's return code $FF00 (a failure to a `:`
; command), and the program ends by the warm boot.

        .include "oodos.inc"

        .export         _exit
        .export         __STARTUP__ : absolute = 1
        .import         zerobss, initlib, donelib, callmain
        .importzp       sp, tmp1

        OODOS_HEADER

        .segment "STARTUP"
        lda     BP_TOP
        sta     sp
        lda     BP_TOP+1
        sta     sp+1
        jsr     zerobss
        jsr     initlib
        jsr     callmain

; void exit(int status): A/X the status
_exit:  phx
        pha
        jsr     donelib
        pla
        plx
        stx     tmp1
        ora     tmp1
        beq     done                    ; 0: the code the CCP set stays
        lda     #<$FF00
        ldx     #>$FF00
        ldy     #P_CODE
        jsr     BDOS
done:   jmp     WBOOT

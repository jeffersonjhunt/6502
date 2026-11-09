; OOLIB's sound modules (lib/pb*.s, L5): is there a card with revision 2's
; registers (the EQ, the vis page)? Its ID read: the module's high nibble,
; a revision of 2 or more. Shared by the modules; not in oolib.inc.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .code

; Carry clear, A kept: revision 2 or later. Carry set, A = PE_NOCARD:
; revision 1, or no sound card (its ID isn't a sound card's).
PBCARD:
        pha
        lda     SND_ID
        eor     #SND_TYPE               ; the module's nibble 0, the revision left
        cmp     #$10
        bcs     @no
        cmp     #2
        bcc     @no
        pla
        clc
        rts
@no:    pla
        lda     #PE_NOCARD
        sec
        rts

; The sound card's records for the diagnostics (stage 10 in block 22, S10 in
; block 21; each block its own copy): a command record sent through CMD, its
; REPLY read back from EVT. The card is revision 3, the device
; (hw/design.md, "Sound card revision 3"): every command gets one REPLY, in order, and
; events met on the way are read and dropped. Interrupts are off for the
; whole run, so nothing else reads EVT. Times are timer 2's (TZERO, TTICK):
; a unit of Y is 65,536 PHI2 cycles, about 9.2 ms.

SND_TAG         = $D1                  ; the diagnostics' tag in every record
SND_100MS       = 11                   ; 720,896 cycles: 100.7 ms
SND_250MS       = 28                   ; 1,835,008 cycles: 256 ms
RBUF            = $3E00                ; W0's: the last REPLY from its +2: the tag, the status, the results

; Until STATUS has a bit of A (carry clear), or Y units (carry set).
SNDWT:
                sta     SN_MASK
                jsr     TZERO
@w:             lda     SOUND_STATUS
                and     SN_MASK
                bne     @yes
                jsr     TTICK
                cpy     T_ACC + 2      ; carry set while Y >= the units gone
                beq     @no
                bcs     @w
@no:            sec
                rts
@yes:           clc
                rts

; A record from A/X (+0 the command, +1 n, +2 the tag, then n bytes), once
; CMD has room for it (100 ms; carry set if never).
SNDCMD:
                sta     SN_PTR
                stx     SN_PTR + 1
                lda     #SOUND_ST_CMDRDY
                ldy     #SND_100MS
                jsr     SNDWT
                bcs     @r
                ldy     #1
                lda     (SN_PTR),y
                clc
                adc     #3
                tax
                ldy     #0
@b:             lda     (SN_PTR),y
                sta     SOUND_CMD
                iny
                dex
                bne     @b
                clc
@r:             rts

; The next REPLY (100 ms for each record read; carry set if none came), from
; its +2 into RBUF; events before it dropped. A its status, Z set for 0.
SNDREP:
                lda     #SOUND_EV_REPLY
; The next record of kind A, the same way (others before it dropped): for an
; event, RBUF+0 its object, +1-4 its stamp, +5 on its payload.
SNDEVT:
                sta     SN_WANT
@rec:           lda     #SOUND_ST_EVT
                ldy     #SND_100MS
                jsr     SNDWT
                bcs     @r
                lda     SOUND_EVT      ; +0 the kind
                sta     SN_MASK
                ldx     SOUND_EVT      ; +1 n
                beq     @next
                ldy     #0
@b:             lda     SOUND_EVT
                sta     RBUF,y
                iny
                dex
                bne     @b
@next:          lda     SN_MASK
                cmp     SN_WANT
                bne     @rec
                clc
                lda     RBUF + 1
@r:             rts

; SNDCMD, then SNDREP: carry set if either timed out, else A the status.
SNDDO:
                jsr     SNDCMD
                bcs     @r
                jmp     SNDREP
@r:             rts

; Y bytes from (SN_PTR) into DATA, by the CPU (a transfer open for them).
SNDDATA:
                ldx     #0
@b:             lda     (SN_PTR,x)
                sta     SOUND_DATA
                inc     SN_PTR
                bne     @n
                inc     SN_PTR + 1
@n:             dey
                bne     @b
                rts

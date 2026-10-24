; Keys for the monitor, from the serial line or the USB keyboard, whichever
; has one (hw/design.md, "USB card registers").
;
; KINIT, at boot: if a USB card answers, let Pause/Break pull /NMI, which
; enters the monitor. KEYHIT: a key now? Carry set and the key in A, or carry
; clear. GETKEY waits for one. Both keep X and Y.
;
; Serial keys come as they are. USB keys come from KEY, cooked, with the
; strobe cleared on the way; lowercase letters are made capitals, since the
; Woz Monitor knows only those (the card's Caps Lock is on anyway, unless
; someone turns it off).

.setcpu "65C02"
.include "backplane.inc"
.export KINIT, KEYHIT, GETKEY

.segment "INPUT"

KINIT:
                LDA     USB_ID
                CMP     #USB_ID_V1
                BNE     @done
                LDA     #USB_NMI_PAUSE
                STA     USB_KBCFG
@done:          RTS

KEYHIT:
                LDA     ACIA_STATUS
                AND     #$08           ; a serial key?
                BEQ     @usb
                LDA     ACIA_DATA
                SEC
                RTS
@usb:           LDA     USB_ID         ; a USB card (read each time: no zero page in the loop)
                CMP     #USB_ID_V1
                BNE     @none
                LDA     USB_KEY
                BPL     @none          ; no strobe: nothing new
                JSR     TAKE
                SEC
                RTS
@none:          CLC
                RTS

; The monitor's wait: the same, inline, so the loop is code and I/O reads only
; (the panel's lamps and the bus trace show it plainly).
GETKEY:
@poll:          LDA     ACIA_STATUS
                AND     #$08
                BNE     @serial
                LDA     USB_ID
                CMP     #USB_ID_V1
                BNE     @poll
                LDA     USB_KEY
                BPL     @poll
                JMP     TAKE
@serial:        LDA     ACIA_DATA
                RTS

; A = KEY with the strobe set: clear it, drop bit 7, make letters capitals.
TAKE:
                BIT     USB_KEYCLR     ; any access clears the strobe (BIT leaves A alone)
                AND     #$7F
                CMP     #'a'
                BCC     @got
                CMP     #'z' + 1
                BCS     @got
                AND     #$DF           ; a capital
@got:           RTS

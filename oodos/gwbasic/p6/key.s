; Partly derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Block P6's keyboard, from the event queue alone (hw/oodos.md, "The OEM
; layer": V2; 7d1). The project's own: the OEM's KEYINP, which on the PC
; maps the BIOS's keys, and its maps; KEYIN's and KEYSIN's waits follow
; GIOKYB.ASM's.
;
;   KEYEV     the resident EVPOLL's far call for each key pressed (A its HID
;             usage, X the modifiers), or repeating (EVPOLL's own repeat,
;             the owner's answer 8): its code queued (KYBQ) as the PC gives
;             it to INKEY$: a character, or 0 and a scan code for a key with
;             none; Ctrl-C the break (A = $80 back, EVPOLL's to take); Ctrl-S
;             the pause, here, until another key (dropped; Ctrl-C breaks);
;             Caps Lock GW-BASIC's own, on at start-up, its light by function
;             81's IN_LEDS. An unshifted F1-F10 (59-68, GIOKYB.ASM's
;             CHKFKY) whose soft key (STRTAB) has a text queues the text
;             instead, whole or not at all if the queue hasn't room for it
;             (7d2, the owner's answer 2; the source expands it as it is
;             read, CHSNS's GETFKY). Then the key's cooked twin drained (W1:
;             while function 11 says ready, function 6 reads and drops). A =
;             $40 back if the key repeats, else 0
;   KEYINI    Caps Lock on, its light
;   KEYSIN    the next byte of the keyboard queue, waited for with the
;             editor's cursor shown, the cooked keys drained each time round
;   KEYIN     the editor's next key: A AL; C set for a two-byte one, X its
;             first byte ($FF: an editor function, SCNDRV.ASM's control
;             codes); a PC two-byte key with no function (a function key with
;             no text, a shifted one) is passed over
;
; The US layout, as OOUI's UCHAR has it; the keypad as digits (Num Lock on).

.include "gwbasic.inc"
.include "p6/p6.inc"

;@entry KEYEV
.export KEYEV, KEYINI
.import CHSNS, PKQUE, KYBQIN, KYBQOUT, EVAREA, TXLEN, TXRUN, STRTAB

K_CTRL          = $11                  ; EV_MODS: left or right
K_SHIFT         = $22
K_ALT           = $44

.code

KEYEV:          STA     KUSE
                STX     KMOD
                CMP     #$39           ; Caps Lock
                BNE     @nc
                LDA     CAPS
                EOR     #$FF
                STA     CAPS
                JSR     KLEDS
                JMP     @none
@nc:            JSR     KXLAT
                JCS     @none
                LDA     KTWO
                BNE     @q
                LDA     KC1
                CMP     #3             ; ^C: the break
                BNE     @ns
                JSR     KDRAIN
                LDA     #$80
                RTS
@ns:            CMP     #$13           ; ^S: the pause, what was printed on
                BNE     @q             ; the screen first (as LIST's lines)
                JSR     KDRAIN
                LDA     TXLEN
                BEQ     :+
                JSR     TXRUN
:               JMP     KPAUSE
@q:             SEC                    ; the queue's room
                LDA     KYBQOUT
                SBC     KYBQIN
                SEC
                SBC     #1
                AND     #KYBQLN - 1
                STA     KROOM
                LDA     KTWO           ; CHKFKY: F1-F10 unshifted, 59-68
                BEQ     @one
                LDA     KC2
                SEC
                SBC     #59
                CMP     #10
                BCS     @one
                ASL     A              ; its text, STRTAB + 16 x n
                ASL     A
                ASL     A
                ASL     A
                TAX
                STX     KSX
                LDY     #0
@len:           LDA     STRTAB,X
                BEQ     :+
                INX
                INY
                BRA     @len
:               TYA
                BEQ     @one           ; none: the PC's code, 0 and the scan code
                CPY     KROOM
                BEQ     :+
                BCS     @full          ; no room for it all: dropped
:               LDX     KSX            ; GETFKY's characters, queued now
@txt:           LDA     STRTAB,X
                BEQ     @full
                JSR     PKQUE          ; (X kept)
                INX
                BRA     @txt
@one:           LDA     KROOM          ; room for both bytes of a two-byte key
                CMP     #2
                BCC     @full
                LDA     KC1
                JSR     PKQUE
                LDA     KTWO
                BEQ     @full
                LDA     KC2
                JSR     PKQUE
@full:          JSR     KDRAIN
                LDA     #$40
                RTS
@none:          JSR     KDRAIN
                LDA     #0
                RTS

; KPAUSE: GIOKYB.ASM's CNTCCN pause: the queue read until a key that isn't
; ^S, dropped; ^C: A = $80 (the break), else 0.
KPAUSE:         LDX     EVAREA + EV_QTAIL
                CPX     EVAREA + EV_QHEAD
                BEQ     KPAUSE
                TXA
                ASL     A
                ASL     A
                ASL     A
                TAY
                INX
                CPX     EVAREA + EV_QSLOTS
                BCC     :+
                LDX     #0
:               LDA     EVAREA + EV_QUEUE + EQ_KIND,Y
                CMP     #EK_KEY
                BNE     @next
                LDA     EVAREA + EV_QUEUE + EQ_VALUE,Y
                BEQ     @next
                LDA     EVAREA + EV_QUEUE + EQ_MODS,Y
                STA     KMOD
                LDA     EVAREA + EV_QUEUE + EQ_CODE,Y
                STA     KUSE
                STX     EVAREA + EV_QTAIL
                JSR     KXLAT
                BCS     KPAUSE
                LDA     KTWO
                BNE     @go
                LDA     KC1
                CMP     #$13
                BEQ     KPAUSE
                CMP     #3
                BNE     @go
                JSR     KDRAIN
                LDA     #$80
                RTS
@go:            JSR     KDRAIN
                LDA     #0
                RTS
@next:          STX     EVAREA + EV_QTAIL
                BRA     KPAUSE

; KXLAT: KUSE, KMOD a key's code: KC1, or (KTWO set) 0 and KC2, the scan
; code. C set: none.
KXLAT:          STZ     KTWO
                LDA     KUSE
                CMP     #$04
                JCC     KNO
                CMP     #$1E
                BCS     KX_pairs
                SEC                    ; a letter
                SBC     #$04
                TAX
                LDA     KMOD
                AND     #K_CTRL
                BEQ     :+
                TXA
                INC     A              ; ^A-^Z
                JMP     KONE
:               LDA     KMOD
                AND     #K_ALT
                BEQ     :+
                LDA     ALTLET,X
                JMP     KTW
:               TXA
                CLC
                ADC     #'a'
                TAX
                LDA     KMOD
                AND     #K_SHIFT
                BEQ     :+
                LDA     #$FF
:               EOR     CAPS           ; Shift against Caps Lock
                BPL     :+
                TXA
                AND     #$DF
                JMP     KONE
:               TXA
                JMP     KONE
KX_pairs:         CMP     #$39
                BCS     KX_fkeys
                LDA     KMOD
                AND     #K_CTRL
                BEQ     KX_plain
                LDX     #CTLN - 1      ; Ctrl and a key: the PC's few
KX_c:             LDA     CTLU,X
                CMP     KUSE
                BEQ     KX_cf
                DEX
                BPL     KX_c
KNO:            SEC
                RTS
KX_cf:            LDA     CTLC,X
                JNE     KONE
                LDA     #3             ; Ctrl-2: 0, 3
                JMP     KTW
KX_plain:         LDA     KMOD
                AND     #K_ALT
                BEQ     KX_pl
                LDA     KUSE           ; Alt and a digit: 0, 120-129
                CMP     #$28
                JCS     KNO
                ADC     #120 - $1E
                JMP     KTW
KX_pl:            LDA     KUSE
                CMP     #$2B           ; Shift-Tab: 0, 15
                BNE     :+
                LDA     KMOD
                AND     #K_SHIFT
                BEQ     :+
                LDA     #15
                JMP     KTW
:               LDA     KUSE
                SEC
                SBC     #$1E
                ASL     A
                TAX
                LDA     KMOD
                AND     #K_SHIFT
                BEQ     :+
                INX
:               LDA     KPAIR,X
                JEQ     KNO
KONE:           STA     KC1
                CLC
                RTS
KTW:           STA     KC2
                STZ     KC1
                LDA     #1
                STA     KTWO
                CLC
                RTS
KX_fkeys:         CMP     #$44           ; F1-F10: 59-68, Shift 84, Ctrl 94, Alt 104
                BCS     KX_nav
                CLC
                ADC     #59 - $3A
                TAX
                LDA     KMOD
                AND     #K_ALT
                BEQ     :+
                TXA
                CLC
                ADC     #45
                JMP     KTW
:               LDA     KMOD
                AND     #K_CTRL
                BEQ     :+
                TXA
                CLC
                ADC     #35
                JMP     KTW
:               LDA     KMOD
                AND     #K_SHIFT
                BEQ     :+
                TXA
                CLC
                ADC     #25
                JMP     KTW
:               TXA
                JMP     KTW
KX_nav:           CMP     #$49
                JCC     KNO
                CMP     #$53
                BCS     KX_pad
                SEC
                SBC     #$49
                TAX
                LDA     KMOD
                AND     #K_CTRL
                BEQ     :+
                LDA     NAVC,X
                JMP     KTW
:               LDA     NAVP,X
                JMP     KTW
KX_pad:           CMP     #$54
                JCC     KNO
                CMP     #$64
                JCS     KNO
                SEC
                SBC     #$54
                TAX
                LDA     KPAD,X
                JMP     KONE

; KLEDS: the keyboard's lights (function 81's IN_LEDS): Caps Lock as CAPS, Num Lock on.
KLEDS:          LDA     #IN_LEDS
                STA     LEDB
                LDA     CAPS
                AND     #$02
                ORA     #$01           ; Num Lock's on (the keypad gives digits)
                STA     LEDB + 1
                LDA     #<LEDB
                LDX     #>LEDB
                LDY     #O_INPUT
                JMP     BDOS

KEYINI:         LDA     #$FF
                STA     CAPS
                JMP     KLEDS

; KDRAIN: the cooked keys thrown away (X2: function 11, then 6 for each).
KDRAIN:         LDY     #C_STAT
                JSR     BDOS
                CMP     #0
                BEQ     @x
                LDA     #$FF
                LDY     #C_RAWIO
                JSR     BDOS
                BRA     KDRAIN
@x:             RTS

; KEYSIN (GIOKYB.ASM 293): a byte from the keyboard queue (CHSNS: EVPOLL,
; then the queue). None waiting: the cursor shown where SCNPOS puts it,
; written to the card, and the wait; then the user's cursor (3: none).
KEYSIN:         JSR     CHSNS
                BNE     @got
                JSR     SCNPOS
                JSR     SETCSR
                JSR     FLUSH
@w:             JSR     KDRAIN         ; the editor's wait drains too (W1)
                JSR     CHSNS
                BEQ     @w
@got:           PHA
                LDA     #3
                STA     CSRTYP
                JSR     SETCSR
                PLA
                RTS

; KEYIN (GIOKYB.ASM 271): the editor's key. ESC is the line's delete
; (CHRLDL), as the PC's; 0 and a scan code an editor function by EDSCN.
KEYIN:          JSR     KEYSIN
                CMP     #0
                BNE     @one
                JSR     KEYSIN
                LDX     #EDCNT - 1
@f:             CMP     EDSCN,X
                BEQ     @fn
                DEX
                BPL     @f
                BRA     KEYIN          ; none: passed over (IGNNFK's null function keys)
@fn:            LDA     EDFUN,X
                LDX     #$FF
                SEC
                RTS
@one:           CMP     #27
                BNE     @c
                LDA     #CHRLDL
                LDX     #$FF
                SEC
                RTS
@c:             LDX     #0
                CLC
                RTS

.rodata
; KPAIR: usages $1E-$38, unshifted and shifted; 0 none.
KPAIR:          .byte   "1!2@3#4$5%6^7&8*9(0)"
                .byte   13, 13, 27, 27, 8, 8, 9, 9, ' ', ' '
                .byte   "-_=+[{]}\|", 0, 0, ";:'", '"', "`~,<.>/?"
.assert * - KPAIR = 2 * ($39 - $1E), error, "KPAIR: two a usage"
; Ctrl and these: the PC's codes (0: Ctrl-2's two-byte 0, 3).
CTLU:           .byte   $1F, $23, $2D, $2F, $30, $31, $28, $2A
CTLC:           .byte   0, 30, 31, 27, 29, 28, 10, 127
CTLN            = * - CTLC
; ALTLET: Alt and a letter's scan code.
ALTLET:         .byte   30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50
                .byte   49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44
; NAVP, NAVC: Insert, Home, Page Up, Delete, End, Page Down, Right, Left,
; Down, Up ($49-$52), and with Ctrl.
NAVP:           .byte   82, 71, 73, 83, 79, 81, 77, 75, 80, 72
NAVC:           .byte   82, 119, 132, 83, 117, 118, 116, 115, 80, 72
KPAD:           .byte   "/*-+", 13, "1234567890."
; EDSCN, EDFUN: the editor's keys (the IBM editor's): Up, Down, Left,
; Right, Home, End, Ins, Del, Ctrl-Left, Ctrl-Right, Ctrl-End, Ctrl-Home.
EDSCN:          .byte   72, 80, 75, 77, 71, 79, 82, 83, 115, 116, 117, 119
EDFUN:          .byte   CHRUP, CHRDWN, CHRREG, CHRADV, CHRHOM, CHRAPP, CHRINS, CHRDEL
                .byte   CHRBKW, CHRFDW, CHRCLE, CHRERA
EDCNT           = * - EDFUN

.segment "BRAM"
KUSE:           .res    1
KMOD:           .res    1
KC1:            .res    1
KC2:            .res    1
KTWO:           .res    1
KROOM:          .res    1              ; the queue's room
KSX:            .res    1              ; the soft key's text, its place in STRTAB
CAPS:           .res    1              ; bit 7: Caps Lock on
LEDB:           .res    2              ; function 81's block

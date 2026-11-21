; The serial line's receive buffer (hw/oodos.md, "OODOS: a receive buffer
; for the serial line"): a ring a port, filled by the dispatcher, emptied
; by the BIOS (TTYIST, TTYIN). COM1 alone for now, on the stand-in W65C51N; written so that the
; TL16C552A is another back end: only RXPUT, SRPULL, RXFLUSH and RXDEAF
; (below) touch the chip.
;
; The ring: RXBUF's 256 bytes in the system block (sys.inc), 255 usable,
; head = tail empty. The producer (the interrupt, or SRPULL with interrupts
; off) writes only RXHEAD, the consumer (TTYIN) only RXTAIL, so neither
; turns interrupts off to move its own. Nothing is discarded: with the ring
; full the byte stays in the chip and the receiver's interrupt goes off
; (RXFLG's RX_STOPPED); TTYIN, taking a byte, finds the interrupt off and
; pulls the chip's byte in (SRPULL), the interrupt back on. Only the chip's
; overrun loses bytes, counted in RXLOST where its OVRN says so (at least
; one each). The warm boot and DEVINI empty the ring (RXFLUSH).
;
; Each arrival also counts in the event area's EV_RXCNT, if one is open.

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.import DEAFEN, EVON, EVBLK
.importzp EVP
.export SERID6, RXDEAF, SRPULL, RXFLUSH
.export RXHEAD, RXTAIL, RXLOST, RXFLG

.segment "SYSBSS"
RXHEAD:         .res    1              ; the next slot the interrupt writes
RXTAIL:         .res    1              ; the next slot TTYIN reads
RXLOST:         .res    2              ; overruns the chip flagged (OVRN seen), since the last flush
RXFLG:          .res    1              ; RX_STOPPED: the ring full, the receiver's interrupt off

.segment "RES2"

; ID 6, not the tick alone (irq.s's DEAF62, R62_ID6): the serial line's
; byte into the ring, if it is the 6551 interrupting; else DEAFEN, as
; before. With the receiver stopped the 6551 isn't interrupting (its
; interrupt is off) and its byte stays where it is. Interrupts off.
SERID6:         LDA     RXFLG
                BNE     @deaf
                LDA     ACIA_STATUS    ; its flag cleared
                BIT     #ACIA_RDRF
                BNE     RXPUT
@deaf:          JMP     DEAFEN

; The chip's byte into the ring, A its STATUS as read with RDRF set; or, the
; ring full, the byte left in the chip, its interrupt off, RX_STOPPED. The
; event area's EV_RXCNT counts it (W1 on the area's block and back, as TICK
; writes EV_MS). Interrupts off; W0 the system block. Uses A, X and Y.
RXPUT:          LDY     RXHEAD
                INY
                CPY     RXTAIL
                BEQ     @full
                AND     #ACIA_OVRN     ; the chip lost one before this (data's read clears it)
                BEQ     @take
                INC     RXLOST
                BNE     @take
                INC     RXLOST + 1
@take:          LDX     RXHEAD
                LDA     ACIA_DATA
                STA     RXBUF,X
                STY     RXHEAD
                LDA     EVON
                BEQ     @done
                LDX     NB_WIN1
                LDA     EVBLK
                STA     NB_WIN1
                LDY     #EV_RXCNT
                LDA     (EVP),Y
                INC     A
                STA     (EVP),Y
                STX     NB_WIN1
@done:          RTS
@full:          LDA     ACIA_CMD
                ORA     #ACIA_CMD_RXOFF
                STA     ACIA_CMD
                LDA     #RX_STOPPED
                STA     RXFLG
                RTS

; The receiver started again: not stopped, its interrupt on, then a byte
; the chip holds taken (one that came while the interrupt was off raises
; nothing when it goes on). From TTYIST and TTYIN (bios.s), with room in
; the ring; interrupts off.
SRPULL:         STZ     RXFLG
                LDA     ACIA_CMD
                AND     #<~ACIA_CMD_RXOFF
                STA     ACIA_CMD       ; on first: a byte after this interrupts
                LDA     ACIA_STATUS
                BIT     #ACIA_RDRF
                BNE     RXPUT
                RTS

; The ring emptied, RXLOST 0, the receiver on, and a byte the chip held
; dropped: the warm boot's (WBDMA, after DEAFEN), DEVINI's and SR_FLUSH's.
RXFLUSH:        PHP
                SEI
                STZ     RXHEAD
                STZ     RXTAIL
                STZ     RXLOST
                STZ     RXLOST + 1
                STZ     RXFLG
                LDA     ACIA_CMD
                AND     #<~ACIA_CMD_RXOFF
                STA     ACIA_CMD       ; on first: a byte after the read below interrupts
                LDA     ACIA_STATUS
                LDA     ACIA_DATA      ; (its OVRN with it)
                PLP
                RTS

; DEAFEN's part (irq.s): the 6551's command as OODOS has it, a program's
; changes undone: the receiver's interrupt on (off while RX_STOPPED), the
; transmitter's off (RTS low), the rest kept; its flag cleared, and a byte
; it was interrupting for taken, since nothing would raise it again.
RXDEAF:         LDA     ACIA_CMD
                AND     #<~(ACIA_CMD_TXMASK | ACIA_CMD_RXOFF)
                ORA     #ACIA_CMD_TXOFF
                ORA     RXFLG          ; RX_STOPPED is the receiver's interrupt off
                STA     ACIA_CMD
                LDA     ACIA_STATUS
                LDX     RXFLG
                BNE     @done
                BIT     #ACIA_RDRF
                BEQ     @done
                JMP     RXPUT
@done:          RTS
.assert RX_STOPPED = ACIA_CMD_RXOFF, error, "RXDEAF ORs RXFLG into the command"

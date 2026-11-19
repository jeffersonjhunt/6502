; Block P6's part of DEF SEG's screen page (7d3; the project's own), linked
; last so that none of P6's timed paths moves.

.include "gwbasic.inc"
.include "p6/p6.inc"

;@entry SCPAGP
.export SCPAGP
.import APG, CROW, RWOLO, RWOHI

.code

; SCPAGP (7d3): DEF SEG's screen page, for P5's DSBYTE and DSRUN: TXBUF's
; bytes out and the cached row written back (P6IN, P6OUT), and with C set
; (P5 will write the page) the cache forgotten. A/X/Y the active page's card
; address, GXT its reach in bytes: LINCNT rows, or the PC's page (4,096 at
; 80 columns, 2,048 at 40) with 25.
SCPAGP:         PHP
                JSR     P6IN
                JSR     P6OUT
                PLP
                BCC     :+
                STZ     CROW
:               LDX     LINCNT
                LDA     RWOLO + 1,X
                STA     GXT
                LDA     RWOHI + 1,X
                CPX     #25
                BNE     :+
                STZ     GXT
                LDA     #$10
                LDX     CRTWID
                CPX     #80
                BEQ     :+
                LSR     A
:               STA     GXT + 1
                LDA     APG
                LDX     APG + 1
                LDY     APG + 2
                RTS

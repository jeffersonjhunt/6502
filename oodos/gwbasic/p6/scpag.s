; Block P6's part of DEF SEG's screen page (7d3; the project's own), linked
; last so that none of P6's timed paths moves.

.include "gwbasic.inc"
.include "p6/p6.inc"

;@entry SCPAGP
;@entry KEYDP
;@entry LOCLIM
.export SCPAGP, KEYDP, LOCLIM
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

; KEYDP (7e5: the space plan's M6): after the home block's KEY n,"text"
; (p8/far6.s) has written the key's text (STRTAB), the key line drawn again
; if it is on (GWSTS.ASM's KEY's end); else the last line as it is.
KEYDP:          JSR     P6IN
                LDA     KEYSW
                BEQ     :+
                JSR     KEYDSP
:               JMP     P6OUT

; LOCLIM (7e5: the owner's answer 13): for the home block's LOCATE (p8/far6.s),
; A the last row a line may name: LINCNT, less 1 with the key line on (KEYSW
; $FF), as LOCATP checks it.
LOCLIM:         LDA     LINCNT
                CLC
                ADC     KEYSW
                RTS

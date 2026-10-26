; Derived from Microsoft GW-BASIC (1983), MIT licence, see LICENSE.
;
; BIMISC.ASM's SYNCHR, GETSTK and ISLET2, translated (segment BIMISC), and
; the BASIC stack (segment RUNTIME), which has no 8086 counterpart: it is
; what the 8086's PUSH and POP of BASIC's own data become (gwbasic.inc).

.include "gwbasic.inc"

.export SYNCHR, GETSTK, ISLET2, SPUSH, SPOP, SPUSHA, SPOPA
.import CHRGTR, SNERR, OMERR

NUMLEV          = 16                   ; words GETSTK keeps spare, as the 8086's NUMLEV

.segment "BIMISC"

; SYNCHR: JSR SYNCHR / .byte c. The character at TXTPTR must be c (else
; Syntax error); then CHRGTR past it. Returns past the inline byte. Reading
; the return address with PLA is a stack instruction, which OODOS's rule 3
; allows.
SYNCHR:         PLA
                STA     SYNPTR
                PLA
                STA     SYNPTR+1
                INCW    SYNPTR         ; the inline byte
                LDA     (TXTPTR)
                CMP     (SYNPTR)
                BNE     SYNERR
                LDA     SYNPTR+1
                PHA
                LDA     SYNPTR
                PHA
                JMP     CHRGTR
SYNERR:         JMP     SNERR

; GETSTK: make sure the BASIC stack has room for A more words and NUMLEV
; spare, and the 6502's stack a little, or Out of memory.
GETSTK:         ASL     A
                ADC     #2*NUMLEV
                STA     SPN
                SEC
                LDA     SSP
                SBC     SPN
                TAY
                LDA     SSP+1
                SBC     #0
                BCC     @om
                CMP     STKLOW+1
                BCC     @om
                BNE     @hw
                CPY     STKLOW
                BCC     @om
@hw:            TSX
                CPX     #$40
                BCC     @om
                RTS
@om:            JMP     OMERR

; ISLET2: C clear if A is a letter.
ISLET2:         CMP     #'A'
                BCC     @no
                CMP     #'Z'+1
                RTS
@no:            SEC
                RTS

.segment "RUNTIME"

; SPUSH: A bytes from zero page at X onto the BASIC stack, as one block.
; SPOP: the top block of A bytes back to zero page at X. A block pushed
; with SPUSH comes back whole with SPOP of the same length.
SPUSH:          STA     SPN
                SEC
                LDA     SSP
                SBC     SPN
                STA     SSP
                BCS     :+
                DEC     SSP+1
:               LDY     #0
@m:             LDA     0,X
                STA     (SSP),Y
                INX
                INY
                CPY     SPN
                BNE     @m
                RTS
SPOP:           STA     SPN
                LDY     #0
@m:             LDA     (SSP),Y
                STA     0,X
                INX
                INY
                CPY     SPN
                BNE     @m
                CLC
                LDA     SSP
                ADC     SPN
                STA     SSP
                BCC     :+
                INC     SSP+1
:               RTS

; SPUSHA, SPOPA: one byte, in A; SPOPA sets N and Z by it.
SPUSHA:         PHA
                LDA     SSP
                BNE     :+
                DEC     SSP+1
:               DEC     SSP
                PLA
                STA     (SSP)
                RTS
SPOPA:          LDA     (SSP)
                INCW    SSP
                ORA     #0
                RTS

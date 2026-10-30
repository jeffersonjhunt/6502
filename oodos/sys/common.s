; OODOS's common memory, $F800-$FFFF (hw/oodos.md, "The system, phase 2"):
; the same 2 KB in every block W3 maps, so it is there whatever block a
; program has in W3, and code here can switch W3 under itself (the next
; instruction comes from the new block's copy, at the same address).
;
;   $F800   the BIOS's jump table: CP/M 3's 30 entries in CP/M 3's order
;           (system.inc's BIOS), for the BDOS: they jump into block 61, so
;           they work only with the system's windows. Programs use function 50.
;           Each but BOOT and WBOOT goes through its own entry below, which
;           puts W2 on the BIOS for the call and back as it was, so the BDOS
;           may keep a hash or buffer block in W2 (hw/oodos.md, "The BDOS,
;           phase 3"); CALLW2 says which, for bank 0's $8000-$BFFF
;   STUB    the BDOS's entry ($BFFD jumps here, $0203 jumps there)
;   WARM    the warm boot: the program's windows, a fresh stack, the CCP
;   FARCALL a call into another block through W3 (from oodos/spike/gwbasic/farcall.s)
;   IRQ, NMI and the vectors
;
; Common memory keeps no state and is never written, so a program may have
; it in W3 read-only, or in flash: the stub keeps what it must on the
; program's own stack (three bytes under its P on the way in, A and X on the
; way out), with interrupts off. FARCALL's operand is its own, in the
; caller's block, for the length of one call.

.setcpu "65C02"
.include "sys.inc"
.import COLD, CCP, BDOSENT
.import CONST, CONIN, CONOUT, LIST, AUXOUT, AUXIN, HOME, SELDSK, SETTRK, SETSEC
.import SETDMA, READ, WRITE, LISTST, SECTRN, CONOST, AUXIST, AUXOST, DEVTBL, DEVINI
.import DRVTBL, MULTIO, FLUSH, MOVE, TIME, SELMEM, SETBNK, XMOVE
.importzp DMABANK, CURBANK, XPEND
.import DMASEG, AMAP
.export STUB, STUBRET, WARM, FARCALL, FCRET, IRQ, NMI

.segment "JUMPS"
JUMPS:          JMP     COLD           ; BOOT
                JMP     WARM           ; WBOOT
                JMP     W_CONST
                JMP     W_CONIN
                JMP     W_CONOUT
                JMP     W_LIST
                JMP     W_AUXOUT
                JMP     W_AUXIN
                JMP     W_HOME
                JMP     W_SELDSK
                JMP     W_SETTRK
                JMP     W_SETSEC
                JMP     W_SETDMA
                JMP     W_READ
                JMP     W_WRITE
                JMP     W_LISTST
                JMP     W_SECTRN
                JMP     W_CONOST
                JMP     W_AUXIST
                JMP     W_AUXOST
                JMP     W_DEVTBL
                JMP     W_DEVINI
                JMP     W_DRVTBL
                JMP     W_MULTIO
                JMP     W_FLUSH
                JMP     W_MOVE
                JMP     W_TIME
                JMP     W_SELMEM
                JMP     W_SETBNK
                JMP     W_XMOVE
.assert * = BIOS_ENTRY(BIOS::COUNT), error, "the jump table is CP/M 3's 30 entries"

.segment "COMMON"

; The jump table's entries: each calls its BIOS entry with W2 on the BIOS
; (block 61), CALLW2 the block its caller had there, and puts that block
; back in W2 after, keeping A, X, Y and the carry the entry returns (N and Z
; then follow A). The BIOS calls none of them itself, so CALLW2 is its
; caller's. A goes through JTA, as the stack holds the old W2.
.macro BIOSW target
                STA     JTA
                LDA     NB_WIN2
                STA     CALLW2
                PHA
                LDA     #BIOS_BLOCK
                STA     NB_WIN2
                LDA     JTA
                JSR     target
                JMP     W2BACK
.endmacro
W_CONST:        BIOSW   CONST
W_CONIN:        BIOSW   CONIN
W_CONOUT:       BIOSW   CONOUT
W_LIST:         BIOSW   LIST
W_AUXOUT:       BIOSW   AUXOUT
W_AUXIN:        BIOSW   AUXIN
W_HOME:         BIOSW   HOME
W_SELDSK:       BIOSW   SELDSK
W_SETTRK:       BIOSW   SETTRK
W_SETSEC:       BIOSW   SETSEC
W_SETDMA:       BIOSW   SETDMA
W_READ:         BIOSW   READ
W_WRITE:        BIOSW   WRITE
W_LISTST:       BIOSW   LISTST
W_SECTRN:       BIOSW   SECTRN
W_CONOST:       BIOSW   CONOST
W_AUXIST:       BIOSW   AUXIST
W_AUXOST:       BIOSW   AUXOST
W_DEVTBL:       BIOSW   DEVTBL
W_DEVINI:       BIOSW   DEVINI
W_DRVTBL:       BIOSW   DRVTBL
W_MULTIO:       BIOSW   MULTIO
W_FLUSH:        BIOSW   FLUSH
W_MOVE:         BIOSW   MOVE
W_TIME:         BIOSW   TIME
W_SELMEM:       BIOSW   SELMEM
W_SETBNK:       BIOSW   SETBNK
W_XMOVE:        BIOSW   XMOVE
W2BACK:         STA     JTA            ; the caller's W2 back
                PLA
                STA     NB_WIN2
                LDA     JTA
                RTS

; The BDOS's entry: the program's JSR $0203 lands here through $BFFD, the
; function in Y, the parameter in A and X. The program's P, A, X and Y go on
; its own stack, and WIN0 into A and its S into X before W0 changes; then its
; S and WIN0-WIN3 into the system's zero page (ST_*), A and X read back off
; its stack through W1, W0-W3 become 31, 60, 61 and 62 and S a fresh $FF. The
; BDOS returns A and X; the program gets them back with its windows, S and P,
; D clear, Y not kept.
STUB:           PHP
                SEI
                PHA
                PHX
                PHY
                TSX                    ; $0101,X Y, X, A, then P
                LDA     NB_WIN0
                LDY     #SYS_BLOCK
                STY     NB_WIN0        ; the system's zero page and stack from here
                STA     ST_W0
                LDA     NB_WIN1
                STA     ST_W1
                LDA     ST_W0
                STA     NB_WIN1        ; the program's stack page at $4100
                LDY     $4101,X
                LDA     $4102,X
                STA     ST_X
                LDA     $4103,X
                STA     ST_A
                INX                    ; its S after the PHP
                INX
                INX
                STX     ST_S
                LDA     NB_WIN2
                STA     ST_W2
                LDA     NB_WIN3
                STA     ST_W3
                LDA     #BDOS_BLOCK
                STA     NB_WIN1
                LDA     #BIOS_BLOCK
                STA     NB_WIN2
                LDA     #RES_BLOCK
                STA     NB_WIN3        ; block 62's copy of this, from the next instruction
                LDX     #$FF
                TXS
                CLD
                LDA     ST_A
                LDX     ST_X
                JSR     BDOSENT
STUBRET:        STA     ST_A           ; the results go on the program's stack,
                STX     ST_X           ; under its P
                LDA     ST_W0
                STA     NB_WIN1        ; its stack page at $4100, the BDOS done with W1
                LDY     ST_S
                LDA     ST_X
                STA     $4100,Y        ; X at $0100 + S, A under it, for PLA and PLX
                DEY
                LDA     ST_A
                STA     $4100,Y
                DEY
                LDA     ST_W1
                STA     NB_WIN1
                LDA     ST_W2
                STA     NB_WIN2
                TYA
                TAX
                LDY     ST_W3
                LDA     ST_W0
                STA     NB_WIN0        ; the program's zero page and stack
                TXS
                STY     NB_WIN3        ; the program's block, its copy of this
                PLA
                PLX
                PLP
                CLD
                RTS

; The warm boot: from $0200, function 0 and the BIOS's WBOOT, in any window
; state. The BIOS's banks back to 0 and no XMOVE pending, the BDOS's DMA
; segment none and its memory blocks free (phase 3c), so nothing a program
; set reaches the next; then the program's windows (blocks 0-2, 62),
; VEC on 62, a fresh stack, D clear and interrupts off, and the CCP.
WARM:           SEI
                CLD
                LDA     #RES_BLOCK
                STA     NB_WIN3        ; this code goes on, in block 62's copy
                STA     NB_VEC
                LDA     #SYS_BLOCK
                STA     NB_WIN0
                STZ     DMABANK        ; BANK_SYS: .assert below
                STZ     CURBANK
                STZ     XPEND
                LDA     #$FF           ; no DMA segment (51), and every block freed (58):
                STA     DMASEG         ; what a program set or took ends with it
                STA     DMASEG + 1
                LDX     #AMAP_LEN - 1
@free:          STZ     AMAP,X
                DEX
                BPL     @free
                STZ     NB_WIN0
                LDA     #1
                STA     NB_WIN1
                LDA     #2
                STA     NB_WIN2
                LDX     #$FF
                TXS
                JMP     CCP

.assert BANK_SYS = 0, error, "WARM's STZs set bank 0"

; FARCALL, the spike's (oodos/spike/gwbasic/farcall.s), unchanged but for
; where it lives. Found through function 72.
;
;   JSR FARCALL / .byte block / .word target
;
; maps the block into W3, calls the target, and maps the caller's block
; back. A, X, Y and the flags reach the target as the caller left them, and
; come back as the target left them. The old block goes on the 6502's stack,
; so far calls nest. The block must have common memory in its top 2 KB, as
; OODOS puts there in a block allocated for code. This is OODOS's code, not
; a program's: it reads its frame in page 1 by address, and modifies its own
; operand, in the block it is leaving.
FARCALL:        PHP
                CLD                    ; binary adds below; PLP gives the caller's D back
                PHA
                PHX
                PHY
                TSX                    ; $0101,X Y, X, A, P, then the return address
                LDA     $0105,X        ; the inline bytes follow it
                STA     FCRD+1
                LDA     $0106,X
                STA     FCRD+2
                CLC                    ; return past them
                LDA     $0105,X
                ADC     #3
                STA     $0105,X
                BCC     :+
                INC     $0106,X
:               TXA                    ; 5 bytes more, under the registers
                SEC
                SBC     #5
                TAX
                TXS
                LDA     $0106,X        ; the registers down to the top
                STA     $0101,X
                LDA     $0107,X
                STA     $0102,X
                LDA     $0108,X
                STA     $0103,X
                LDA     $0109,X
                STA     $0104,X
                LDA     NB_WIN3        ; the caller's block, for FCRET
                STA     $0109,X
                LDA     #<(FCRET-1)
                STA     $0107,X
                LDA     #>(FCRET-1)
                STA     $0108,X
                LDY     #2             ; the target, less one, for RTS
                JSR     FCRD
                SEC
                SBC     #1
                STA     $0105,X
                INY
                JSR     FCRD
                SBC     #0
                STA     $0106,X
                LDY     #1             ; the block
                JSR     FCRD
                STA     NB_WIN3
                PLY
                PLX
                PLA
                PLP
                RTS                    ; to the target
FCRD:           LDA     $FFFF,Y        ; the operand is the call's return address
                RTS
FCRET:          PHP                    ; the target's return: the old block back
                PHA
                PHX                    ; TSX needs X
                TSX                    ; $0101,X X, A, P, the block, the return
                LDA     $0104,X
                STA     NB_WIN3
                LDA     $0103,X        ; drop the block from under X, A and P
                STA     $0104,X
                LDA     $0102,X
                STA     $0103,X
                LDA     $0101,X
                STA     $0102,X
                PLA                    ; the stale copy of X
                PLX
                PLA
                PLP
                RTS

; IRQ and BRK. BRK is the monitor, as BIOS 0.9's; any other IRQ returns at
; once (phase 2 enables none: the console is polled).
IRQ:            PHA
                PHX
                TSX
                LDA     $0103,X        ; the P the interrupt pushed
                AND     #$10           ; B: a BRK
                BNE     NMI
                PLX
                PLA
                RTI

; Break (NMI) and BRK: into the ROM's monitor, as BIOS 0.9's Break does it,
; with W3 and VEC back on block 63 (read-only), where the ROM's copy is. This
; code is in W3, so it can't switch W3 and go on: W0 goes to the system block
; (the monitor starts afresh, so the program's zero page and stack don't
; matter), TRAMP is written there anew, and TRAMP switches W3 and VEC and
; jumps through block 63's NMI vector to BREAK, which puts W0-W2 back on
; blocks 0-2.
NMI:            SEI
                LDA     #SYS_BLOCK
                STA     NB_WIN0
                LDX     #TRAMPEND - TRAMPSRC - 1
@copy:          LDA     TRAMPSRC,X
                STA     TRAMP,X
                DEX
                BPL     @copy
                JMP     TRAMP
TRAMPSRC:       LDA     #NB_RO | ROM_BLOCK ; position-independent: it runs at TRAMP
                STA     NB_WIN3
                STA     NB_VEC
                JMP     ($FFFA)        ; block 63's: the ROM's BREAK
TRAMPEND:

.segment "VECTORS"
                .word   NMI
                .word   WARM           ; RESET: VEC is 62 only while OODOS runs, so never taken
                .word   IRQ

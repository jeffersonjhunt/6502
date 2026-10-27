; OODOS's stand-in CCP (hw/oodos.md, "The system, phase 2"), in block 62's
; code window. Phase 4's CCP replaces it.
;
; It runs as a program does: the program's windows (blocks 0-2, W3 on 62),
; its zero page and stack, the BDOS through $0203. The warm boot (common.s's
; WARM) comes here with a fresh stack. It sets the base page and $BFFD,
; prompts A> (the current drive's letter), reads a line with function 10, and
; runs its first word:
;
;   GO      the program image already at $0300, its first record (the
;           header) at $0280 where a loader leaves it: for tests, and for
;           whatever put it there by hand. The command tail goes over the
;           header, so a second GO needs it put back
;   NAME    NAME.COM from drive A, user 0: the integration hook below.
;           Until then, and for a name it can't find, NAME?
;
; Before a program runs, its header is checked as the loader checks it
; (hw/oodos.md, "The program header"): the magic number, no flags (a .COM),
; link page $03, and the image and its BSS below the top of the TPA. Then
; the command tail goes to $0280 (its length, then the rest of the line,
; its leading space too), the default FCBs are blanked, the stack is fresh
; with the warm boot's address on it (a program's RTS ends it), and it
; starts at $0300.

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.import WARM, STUB
.export CCP, RUNHDR

LINE_MAX        = 126                  ; what $0280's tail can hold

.segment "RESBSS"
LINE:           .res    2 + LINE_MAX   ; function 10's: its size, the count, the line
WSTART:         .res    1              ; the command word, in LINE
WEND:           .res    1
FCBNAME:        .res    11             ; NAME.COM as an FCB has it, for the hook

.segment "RES"

CCP:            LDX     #2             ; the base page's JMPs, and $BFFD's
@jmps:          LDA     BASEJMPS,X
                STA     BP_WBOOT,X
                LDA     BASEJMPS + 3,X
                STA     BP_BDOS,X
                LDA     BASEJMPS + 6,X
                STA     BDOS_ENTRY,X
                DEX
                BPL     @jmps
PROMPT:         LDX     #$FF
                TXS
                LDA     #<CRLF
                LDX     #>CRLF
                JSR     STRING
                LDY     #DRV_GET
                JSR     BDOS
                CLC
                ADC     #'A'
                JSR     PUTC
                LDA     #'>'
                JSR     PUTC
                LDA     #LINE_MAX
                STA     LINE
                LDA     #<LINE
                LDX     #>LINE
                LDY     #C_READSTR
                JSR     BDOS
                LDA     #$0A           ; function 10 echoed the CR
                JSR     PUTC
                LDX     #0             ; X: the line's index
@space:         CPX     LINE + 1
                BEQ     PROMPT         ; nothing on it
                LDA     LINE + 2,X
                CMP     #' '
                BNE     @word
                INX
                BRA     @space
@word:          STX     WSTART
@upper:         LDA     LINE + 2,X
                JSR     UPPER
                STA     LINE + 2,X
                INX
                CPX     LINE + 1
                BEQ     @end
                LDA     LINE + 2,X
                CMP     #' '
                BNE     @upper
@end:           STX     WEND
                TXA                    ; GO?
                SEC
                SBC     WSTART
                CMP     #2
                BNE     LOADCOM
                LDX     WSTART
                LDA     LINE + 2,X
                CMP     #'G'
                BNE     LOADCOM
                LDA     LINE + 3,X
                CMP     #'O'
                BNE     LOADCOM
                JMP     RUNHDR

; ---- Integration hook: NAME.COM from drive A ----------------------------
; With OOLDR's read-only reader (cpmread.s): CR_INIT with A = 1 (drive A's
; unit), CR_FIND with FCBNAME (made here: the name, space padded to 8, then
; COM), then CR_READ a record at a time: the first to $0280 (the header,
; checked by RUNHDR) and the image, exactly its size, to $0300, as
; hw/oodos.md's "The program header" says; then JMP RUNHDR. Not found:
; NAME?, below.
LOADCOM:        LDY     #10            ; FCBNAME: "NAME    COM"
@blank:         LDA     COMNAME,Y
                STA     FCBNAME,Y
                DEY
                BPL     @blank
                LDX     WSTART
                LDY     #0
@name:          CPX     WEND
                BEQ     NONAME
                CPY     #8
                BCS     NONAME         ; too long: NAME? anyway
                LDA     LINE + 2,X
                STA     FCBNAME,Y
                INX
                INY
                BRA     @name
; ---- (end of the hook) --------------------------------------------------

; NAME?: the word as typed, in capitals, and a question mark.
NONAME:         LDX     WSTART
@char:          LDA     LINE + 2,X
                PHX
                JSR     PUTC
                PLX
                INX
                CPX     WEND
                BNE     @char
                LDA     #'?'
                JSR     PUTC
                JMP     PROMPT

; Run the program whose header is at $0280 and image at $0300: the loader's
; checks, the tail and FCBs, a fresh stack, $0300.
RUNHDR:         LDX     #3
@magic:         LDA     BP_DMA,X
                CMP     MAGIC,X
                BNE     @none
                DEX
                BPL     @magic
                LDA     BP_DMA + OH_FLAGS
                BNE     @bad           ; a .PRL, an RSX or unknown flags
                LDA     BP_DMA + OH_PAGE
                CMP     #>BP_TPA
                BNE     @bad
                CLC                    ; $0300 + size + BSS <= $BFFD
                LDA     BP_DMA + OH_SIZE
                ADC     BP_DMA + OH_BSS
                TAY
                LDA     BP_DMA + OH_SIZE + 1
                ADC     BP_DMA + OH_BSS + 1
                BCS     @bad
                TAX
                TYA
                CLC
                ADC     #<BP_TPA
                TAY
                TXA
                ADC     #>BP_TPA
                BCS     @bad
                CPY     #<(BDOS_ENTRY + 1)
                SBC     #>(BDOS_ENTRY + 1) ; carry set: the end is past $BFFD
                BCS     @bad
                BRA     @fits
@none:          LDA     #<NOPROG
                LDX     #>NOPROG
                BRA     @say
@bad:           LDA     #<BADPROG
                LDX     #>BADPROG
@say:           JSR     STRING
                JMP     PROMPT
@fits:          LDY     #0             ; the tail: after the command word
                LDX     WEND
@tail:          CPX     LINE + 1
                BEQ     @tailed
                LDA     LINE + 2,X
                STA     BP_DMA + 1,Y
                INX
                INY
                BRA     @tail
@tailed:        STY     BP_DMA
                LDA     #0             ; FCBs: drive 0, the names blank
                LDX     #BP_DMA - BP_FCB - 1
@zero:          STA     BP_FCB,X
                DEX
                BPL     @zero
                LDA     #' '
                LDX     #10
@blank:         STA     BP_FCB + 1,X
                STA     BP_FCB2 + 1,X
                DEX
                BPL     @blank
                LDX     #$FF
                TXS
                LDA     #>(BP_WBOOT - 1) ; its RTS: the warm boot
                PHA
                LDA     #<(BP_WBOOT - 1)
                PHA
                LDA     #0
                TAX
                TAY
                JMP     BP_TPA

UPPER:          CMP     #'a'
                BCC     @done
                CMP     #'z' + 1
                BCS     @done
                AND     #$DF
@done:          RTS

PUTC:           LDY     #C_WRITE
                JMP     BDOS
STRING:         LDY     #C_WRITESTR
                JMP     BDOS

BASEJMPS:       JMP     WARM           ; $0200
                JMP     BDOS_ENTRY     ; $0203: so $0204 is $BFFD, the TPA's top + 1
                JMP     STUB           ; $BFFD
MAGIC:          .byte   OODOS_MAGIC
COMNAME:        .byte   "        COM"
CRLF:           .byte   $0D, $0A, "$"
NOPROG:         .byte   "NO PROGRAM$"
BADPROG:        .byte   "BAD PROGRAM HEADER$"

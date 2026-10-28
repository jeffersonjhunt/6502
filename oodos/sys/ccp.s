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
;   NAME    NAME.COM from drive A, user 0, through the BDOS (Open, then
;           Read): the first record to LASTREC, its header (the file's first
;           16 bytes) to $0280 and checked, then exactly the image's size,
;           from the file's 17th byte, to $0300: the first record's 112 bytes
;           (or fewer), whole records read straight to their place, a last
;           part record through LASTREC. A name it can't find is NAME?. The
;           user is put back as it was, whether the program runs or not
;
; Each warm boot has the BIOS read the card's units again (function 50's
; SELDSK of A:, a first select), so a unit whose image was swapped while the
; program ran has its drive's buffers dropped by the 48 that follows, not
; written to the new image; then it writes back what the BDOS holds (48)
; and frees the blocks of files a program didn't close, with the buffers
; whose write failed (98). The CCP's own calls return their
; errors (error mode $FF): an error loading is READ ERROR. A program starts
; in the default error mode, with the DMA address $0280.
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
RECORD          = 128

.segment "RESBSS"
LINE:           .res    2 + LINE_MAX   ; function 10's: its size, the count, the line
WSTART:         .res    1              ; the command word, in LINE
WEND:           .res    1
COMFCB:         .res    36             ; A:NAME.COM
FCBNAME         = COMFCB + 1
LEFT:           .res    2              ; the image's bytes still to load
DEST:           .res    2              ; where the next record goes
LASTREC:        .res    RECORD         ; a record of NAME.COM whose bytes are copied
COUNT:          .res    1              ; COPYREC's bytes
USAVE:          .res    1              ; the user, while NAME.COM is loaded from user 0

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
                LDX     #$FF
                TXS
                LDA     #$FF           ; the CCP's errors come back to it
                LDY     #F_ERRMODE
                JSR     BDOS
                LDA     #<RESCANPB     ; the units read again (a first select of
                LDX     #>RESCANPB     ; A:), so 48 drops a swapped unit's buffers
                LDY     #S_BIOS
                JSR     BDOS
                LDA     #0             ; written back, and unclosed files' blocks freed
                LDY     #DRV_FLUSH
                JSR     BDOS
                LDY     #DRV_FREE
                JSR     BDOS
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
                LDA     #$FF           ; the user, to put back after a load
                LDY     #F_USERNUM
                JSR     BDOS
                STA     USAVE
                LDA     WEND           ; GO?
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

; NAME.COM from drive A, user 0, through the BDOS: record 0 to LASTREC,
; its header (the first 16 bytes) to $0280 and checked before anything else
; is written; then exactly the image's size, which follows the header, to
; $0300, so nothing past the image changes.
; An error, or the file ending inside the image: READ ERROR.
RDERR:          JSR     USERBACK
                LDA     #<READERR
                LDX     #>READERR
                JSR     STRING
                JMP     PROMPT

; The user as it was before the command (LOADCOM sets 0).
USERBACK:       LDA     USAVE
                LDY     #F_USERNUM
                JMP     BDOS

NOFILE:         JMP     NONAME         ; NONAME is out of a branch's reach

LOADCOM:        LDX     #36 - 1        ; A:"NAME    COM", the rest 0
@clear:         STZ     COMFCB,X
                DEX
                BPL     @clear
                INC     COMFCB         ; A:
                LDY     #10
@blank:         LDA     COMNAME,Y
                STA     FCBNAME,Y
                DEY
                BPL     @blank
                LDX     WSTART
                LDY     #0
@name:          CPX     WEND
                BEQ     @open
                CPY     #8
                BCS     NOFILE         ; too long: NAME? anyway
                LDA     LINE + 2,X
                STA     FCBNAME,Y
                INX
                INY
                BRA     @name
@open:          LDA     #0             ; user 0
                LDY     #F_USERNUM
                JSR     BDOS
                LDA     #<COMFCB
                LDX     #>COMFCB
                LDY     #F_OPEN
                JSR     BDOS
                CMP     #$FF
                BNE     @found
                CPX     #0             ; not there, or an error
                BEQ     NOFILE
                BRA     RDERR
@found:         LDA     #<LASTREC      ; record 0: the header, then the image's start
                LDX     #>LASTREC
                JSR     NEXTREC
                BCC     @header
                CMP     #1             ; an empty file: no program
                BNE     RDERR
                STZ     BP_DMA
                JSR     CHECKHDR       ; says so, and doesn't come back
@header:        LDX     #OH_LEN - 1    ; the header to $0280, where RUNHDR reads it
@hdr:           LDA     LASTREC,X
                STA     BP_DMA,X
                DEX
                BPL     @hdr
                JSR     CHECKHDR       ; back only if it may run
                LDA     BP_DMA + OH_SIZE
                STA     LEFT
                LDA     BP_DMA + OH_SIZE + 1
                STA     LEFT + 1
                STZ     DEST
                LDA     #>BP_TPA
                STA     DEST + 1
                LDA     LEFT           ; an empty image: nothing to load
                ORA     LEFT + 1
                BEQ     @loaded
                LDA     #OH_LEN        ; the image starts after the header, in record 0
                JSR     COPYREC
@whole:         LDA     LEFT + 1       ; a whole record left: straight to DEST
                BNE     @straight
                LDA     LEFT
                BEQ     @loaded
                BPL     @part
@straight:      LDA     DEST
                LDX     DEST + 1
                JSR     NEXTREC
                BCS     @fail          ; the file ends inside the image
                CLC
                LDA     DEST
                ADC     #RECORD
                STA     DEST
                BCC     @less
                INC     DEST + 1
@less:          SEC
                LDA     LEFT
                SBC     #RECORD
                STA     LEFT
                BCS     @whole
                DEC     LEFT + 1
                BRA     @whole
@part:          LDA     #<LASTREC      ; a last part record, through LASTREC
                LDX     #>LASTREC
                JSR     NEXTREC
                BCS     @fail
                LDA     #0
                JSR     COPYREC
@loaded:        JMP     RUNTAIL
@fail:          JMP     RDERR

; The file's next record to A/X; carry set if none (A = 1, the end) or an
; error (A = $FF).
NEXTREC:        LDY     #F_DMAOFF
                JSR     BDOS
                LDA     #<COMFCB
                LDX     #>COMFCB
                LDY     #F_READ
                JSR     BDOS
                CMP     #1
                RTS

; LASTREC from offset A to its end, or LEFT bytes if fewer (LEFT isn't 0), to
; DEST; both move on.
COPYREC:        CLC                    ; the LDA's operand: LASTREC + A
                ADC     #<LASTREC
                STA     @from + 1
                LDA     #>LASTREC
                ADC     #0
                STA     @from + 2
                LDA     DEST           ; the STA's: DEST
                STA     @to + 1
                LDA     DEST + 1
                STA     @to + 2
                SEC                    ; Y: the bytes, 128 - offset at most
                LDA     #RECORD
                SBC     @from + 1
                CLC
                ADC     #<LASTREC
                LDX     LEFT + 1
                BNE     @n
                CMP     LEFT
                BCC     @n
                LDA     LEFT
@n:             STA     COUNT
                LDY     #0
@byte:
@from:          LDA     $FFFF,Y
@to:            STA     $FFFF,Y
                INY
                CPY     COUNT
                BNE     @byte
                CLC                    ; DEST += COUNT, LEFT -= COUNT
                LDA     DEST
                ADC     COUNT
                STA     DEST
                BCC     @less
                INC     DEST + 1
@less:          SEC
                LDA     LEFT
                SBC     COUNT
                STA     LEFT
                BCS     @done
                DEC     LEFT + 1
@done:          RTS


; NAME?: the word as typed, in capitals, and a question mark.
NONAME:         JSR     USERBACK
                LDX     WSTART
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
RUNHDR:         JSR     CHECKHDR
                BRA     RUNTAIL

; The header at $0280 checked as the loader checks it: back if it may run,
; else what's wrong and the prompt.
CHECKHDR:       LDX     #3
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
                RTS
@none:          LDA     #<NOPROG
                LDX     #>NOPROG
                BRA     @say
@bad:           LDA     #<BADPROG
                LDX     #>BADPROG
@say:           JSR     STRING
                JSR     USERBACK
                JMP     PROMPT         ; a fresh stack: CHECKHDR's return is dropped

; Run it: the DMA address $0280 and the default error mode, the command
; tail over the header, the FCBs, a fresh stack, $0300.
RUNTAIL:        JSR     USERBACK       ; the program runs in the user it was given in
                LDA     #<BP_DMA
                LDX     #>BP_DMA
                LDY     #F_DMAOFF
                JSR     BDOS
                LDA     #0
                LDY     #F_ERRMODE
                JSR     BDOS
                LDY     #0             ; the tail: after the command word
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
RESCANPB:       .byte   BIOS::SELDSK, 0, 0, 0, 0, 0, 0, 0 ; A: 0, X: 0 a first select
MAGIC:          .byte   OODOS_MAGIC
COMNAME:        .byte   "        COM"
CRLF:           .byte   $0D, $0A, "$"
NOPROG:         .byte   "NO PROGRAM$"
BADPROG:        .byte   "BAD PROGRAM HEADER$"
READERR:        .byte   "READ ERROR$"

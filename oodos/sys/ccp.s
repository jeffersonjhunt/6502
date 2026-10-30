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
; of those blocks whose write failed (98). A write-back that fails is said
; as the BDOS says its errors (error mode $FE: "OODOS Error On B: Disk I/O",
; "BDOS Function = 48"), and the warm boot goes on; a buffer of a closed
; file's block is kept for the next 48 to try again. After that the CCP's
; own calls return their errors (error mode $FF): an error loading is READ
; ERROR. A program starts in the default error mode, with the DMA address
; $0280 and a multi-sector count of 1 (set at each warm boot, function 44).
;
; A ^C typed at the prompt while the line is empty is function 10's warm
; boot too, but the CCP resets the disk system for it (the Programmer's
; Guide, 1.6.2), as the warm boot doesn't: function 13, in error mode $FE.
; 13 writes back what it can and drops what it can't, a kept buffer too,
; saying the first failure ("BDOS Function = 13"), and makes every drive
; read-write. The rescan, 48 and 98 aren't run again: they ran at the warm
; boot before the prompt, and 13 rescans itself. ATPROMPT says the warm
; boot came from the prompt's line; a ^C anywhere else (in a program's
; function 10) is the warm boot.
;
; Then, at every warm boot, what the Guide has the CCP set for a program:
; the console mode 0 and the delimiter '$' (its own strings need it too);
; the multi-sector count 1 is set first (44, above), so the CCP's own
; loads have it too. The CCP keeps its own drive and user (CCPDRV,
; CCPUSR: A:, user 0 from the cold boot; the Guide, 1.7 and 2.3.5), which
; a program's 14 or 32 doesn't change: it makes them current again for the
; prompt, the drive by the SCB, so it isn't logged in until it's used.
; Function 47 leaves a request in the SCB ($17): with CCP_CHAINDU the
; program's drive and user become the CCP's; then the command at $0280, to
; its 0, is shown after the prompt as if typed and run. A program starts
; with the return code 0 (108), unless chained to.
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
CHAINF:         .res    1              ; 47's request, as the SCB had it
CHAINED:        .res    1              ; 1: the line is 47's command

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
                LDA     #1             ; a multi-sector count of 1, for its loads and
                LDY     #F_MULTISEC    ; each program (the Guide, 44)
                JSR     BDOS
                LDA     #$FE           ; a write-back that fails is said, and the
                LDY     #F_ERRMODE     ; warm boot goes on
                JSR     BDOS
                LDA     ATPROMPT       ; a ^C at the prompt: 13, not the warm boot's
                BEQ     @warm          ; 48 and 98 (they ran before that prompt)
                STZ     ATPROMPT
                LDY     #DRV_ALLRESET
                JSR     BDOS
                BRA     @own
@warm:          LDA     #<RESCANPB     ; the units read again (a first select of
                LDX     #>RESCANPB     ; A:), so 48 drops a swapped unit's buffers
                LDY     #S_BIOS
                JSR     BDOS
                LDA     #0             ; written back, and unclosed files' blocks freed
                LDY     #DRV_FLUSH
                JSR     BDOS
                LDY     #DRV_FREE
                JSR     BDOS
@own:           LDA     #$FF           ; the CCP's errors come back to it
                LDY     #F_ERRMODE
                JSR     BDOS
                LDA     #0             ; a program's settings: the console mode 0,
                TAX
                LDY     #C_MODE
                JSR     BDOS
                LDA     #'$'           ; the delimiter '$',
                LDY     #C_DELIMIT
                JSR     BDOS
                LDA     #<CHAINPB      ; 47's request, then none
                LDX     #>CHAINPB
                LDY     #S_SCB
                JSR     BDOS
                STA     CHAINF
                LDA     #<NOCHAINPB
                LDX     #>NOCHAINPB
                LDY     #S_SCB
                JSR     BDOS
                LDA     CHAINF         ; the program's drive and user the CCP's?
                AND     #CCP_CHAINDU
                BEQ     @drive
                LDY     #DRV_GET
                JSR     BDOS
                STA     CCPDRV
                LDA     #$FF
                LDY     #F_USERNUM
                JSR     BDOS
                STA     CCPUSR
@drive:         LDA     CCPDRV         ; the CCP's drive (by the SCB: 14 would log
                STA     DISKPB + 2     ; it in) and user
                LDA     #<DISKPB
                LDX     #>DISKPB
                LDY     #S_SCB
                JSR     BDOS
                LDA     CCPUSR
                LDY     #F_USERNUM
                JSR     BDOS
                LDA     CHAINF
                BMI     CHAIN
PROMPT:         LDX     #$FF
                TXS
                STZ     CHAINED
                JSR     SAYPROMPT
                LDA     #LINE_MAX
                STA     LINE
                LDA     #1             ; a ^C now is function 10's warm boot from the
                STA     ATPROMPT       ; prompt
                LDA     #<LINE
                LDX     #>LINE
                LDY     #C_READSTR
                JSR     BDOS
                STZ     ATPROMPT
GOTLINE:        LDA     #$0A           ; function 10 echoed the CR
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

; 47's command: from $0280 to its 0 (LINE_MAX at most) into the line,
; shown after the prompt as if typed.
CHAIN:          LDX     #0
@copy:          LDA     BP_DMA,X
                BEQ     @end
                CPX     #LINE_MAX
                BEQ     @end
                STA     LINE + 2,X
                INX
                BRA     @copy
@end:           STX     LINE + 1
                LDA     #1
                STA     CHAINED
                JSR     SAYPROMPT
                LDX     #0
@show:          CPX     LINE + 1
                BEQ     @shown
                LDA     LINE + 2,X
                PHX
                JSR     PUTC
                PLX
                INX
                BRA     @show
@shown:         LDA     #$0D
                JSR     PUTC
                JMP     GOTLINE

; CR LF and the prompt: the current drive's letter and '>'.
SAYPROMPT:      LDA     #<CRLF
                LDX     #>CRLF
                JSR     STRING
                LDY     #DRV_GET
                JSR     BDOS
                CLC
                ADC     #'A'
                JSR     PUTC
                LDA     #'>'
                JMP     PUTC

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
                LDA     CHAINED        ; the return code 0, unless chained to
                BNE     @code
                LDA     #0
                TAX
                LDY     #P_CODE
                JSR     BDOS
@code:          LDY     #0             ; the tail: after the command word
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
CHAINPB:        .byte   <(SCB_CCPFLAGS - SCB), 0, 0, 0  ;   get 47's request
NOCHAINPB:      .byte   <(SCB_CCPFLAGS - SCB), $FF, 0, 0 ;  and none
DISKPB:         .byte   <(SCB_DISK - SCB), $FF, 0, 0    ;   set the current drive: CCPDRV
ATPROMPT:       .byte   0              ; 1 while function 10 reads the prompt's line: in
                                       ; the file, so 0 from the cold boot
CCPDRV:         .byte   0              ; the CCP's drive and user, A: and 0 from the
CCPUSR:         .byte   0              ; cold boot

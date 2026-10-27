; OODOS's BDOS (hw/oodos.md, "BDOS", "The BDOS, phase 3"), block 60: $4000-
; $7FFF while it runs (W1), the system block in W0, the BIOS in W2 unless
; the BDOS has a hash or buffer block there (the jump table puts the BIOS
; back for each call: common.s). Written from the CP/M 3 manuals'
; descriptions. This part: the entry, the console, time, the BIOS and
; OODOS's own functions, errors, and the program's memory; bdosfile.s has
; the file functions, bdosdisk.s the drives, buffers and the directory.
;
; The stub in common memory calls BDOSENT with the function in Y, the
; parameter in A (and X), and returns A and X to the program (hw/oodos.md,
; "Registers"). A byte result comes back with X 0. Functions here:
;
;    0  P_TERMCPM   the warm boot
;    1  C_READ      A = a console character, echoed
;    2  C_WRITE     A to the console; a tab is spaces to the next eighth column
;    6  C_RAWIO     A = $FF: A = a character, or 0 if none; $FE: A = the
;                   status; $FD: A = a character, waited for; else A out, raw
;    9  C_WRITESTR  the string at A/X, to the SCB's delimiter ('$')
;   10  C_READSTR   a line into the buffer at A/X (0: the DMA address): +0
;                   its size, +1 the count read, then the characters. BS and
;                   DEL take one back, ^X the line; CR or LF ends it, echoed
;                   as CR; ^C at the start of the line is a warm boot
;   11  C_STAT      A = 1 a character waits, 0 none
;   12  S_BDOSVER   A/X = $0031
;   50  S_BIOS      the BIOS parameter block at A/X: +0 the function, +1 A,
;                   +2 X, +3 Y, +4-5 and +6-7 the words (BW1, BW2); returns
;                   the BIOS's A/X. BOOT (0), which would run cold boot again
;                   and wipe M:, and any function past XMOVE return A = X = $FF
;   72  O_VERSION   a 10-byte block at A/X gets OODOS's version (major,
;                   minor), the BIOS's, the memory fitted and the memory for
;                   programs in KB (words), and FARCALL's address; A/X = FARCALL
;  104  T_SET       the DAT at A/X (days, hour, minute in BCD) into the clock,
;                   the seconds 0
;  105  T_GET       the clock into the DAT at A/X; A = the seconds, BCD. A
;                   clock past 2155 (BIOS TIME's carry): A = X = $FF, the DAT
;                   left as it was
;
; and bdosfile.s's 13-37, 40, 42, 43, 45, 46, 48, 98 and 99. Any other
; returns A = X = $FF.
;
; Errors (hw/oodos.md, "The BDOS, phase 3"): DERR with CP/M 3's code. In
; the default error mode it says "OODOS Error On A: Disk I/O" (or Read
; Only, File R/O, Invalid Drive, File Exists, ? in Filename), then "BDOS
; Function = n" and, for a function with an FCB, "File = NAME    .TYP", and
; ends the program by the warm boot; mode $FE says it and returns, $FF only
; returns: A = $FF and X = the code.
;
; The program's memory: FCBs, records and the like by the BIOS's MOVE
; (PIN, POUT: the DMA controller, through the program's windows the stub
; saved); function 9's string, 10's buffer, 50's parameter block, 72's and
; 104/105's a byte at a time through W2 (PGET, PPUT map the block the
; program's own window had there). An address in the I/O hole, $C000-
; $C7FF, isn't memory: the function stops there and returns A = X = $FF
; (hw/oodos.md, "The system, phase 2").

.setcpu "65C02"
.include "bdos.inc"
.import WARM, FARCALL
.export BDOSENT

ASCII_TAB       = $09
ASCII_CTLC      = $03
ASCII_CTLX      = $18
ASCII_BS        = $08
ASCII_DEL       = $7F
ASCII_CR        = $0D
ASCII_LF        = $0A
MEM_FITTED      = 1024                 ; KB
MEM_PROGRAMS    = 320                  ; KB: banks 1-5, for functions 53-58 (later)
NFUNC           = 106                  ; FIDX's functions, 0-105

.zeropage
PA:             .res    1              ; the parameter
PX:             .res    1
PP:             .res    2              ; a program address, for PGET and PPUT
TP:             .res    2              ; where PMAP put it, in W2
COUNT:          .res    1
SIZE:           .res    1
BIOSV:          .res    2
FUNC:           .res    1              ; the function, for an error's message
EDRV:           .res    1              ; the drive an error is on
ECODE:          .res    1
T0:             .res    1              ; scratch
T1:             .res    1
T2:             .res    1
T3:             .res    1
T4:             .res    1
T5:             .res    1
CDRV:           .res    1              ; the drive a function works on (bdosdisk.s)
CSLOT:          .res    1              ; its slot: 0-6 A-G, 7 M:
CD:                                    ; its DTAB entry while it is the one
BSH:            .res    1              ;   the DPB's BSH, BLM, EXM, DSM and DRM
BLM:            .res    1
EXM:            .res    1
DSM:            .res    2
DRM:            .res    2
HWM:            .res    2              ;   entries past the last file, XFCB or label
MODE:           .res    1              ;   the label's mode byte, 0 without one
BIG:            .res    1              ;   bit 7: 16-bit block pointers (DSM > 255)
OFFS:           .res    2              ;   the directory's first sector (OFF x 16)
DPBP:           .res    2              ;   the DPB, in common memory
                .res    CD_LEN - (* - CD)
HBLK:           .res    1              ; the drive's hash block, and its base's high byte
HBASE:          .res    1
ALVP:           .res    2              ; its allocation vector (its permanent half $200 on)
LBA:            .res    2              ; a sector of the drive (GETBUF)
BPTR:           .res    2              ; GETBUF's buffer, in W2
DPTR:           .res    2              ; DIRENT's entry, in W2
DIDX:           .res    2              ; a directory entry's number
HP:             .res    2              ; its hash entry, in W2
LIM:            .res    2              ; FIND's last entry + 1
HV:             .res    2              ; a name's hash (NHASH)
H0:             .res    1              ; an entry's hash entry: its first byte, ...
H3:             .res    1              ; ... and its extent byte
NP:             .res    2              ; NHASH's name
BLK:            .res    2              ; a block
RECP:           .res    2              ; a record, in its buffer
GBNEW:          .res    1              ; GETBUF: 1 a new sector, zeroed
LASTB:          .res    1              ; the buffer GETBUF gave last
BIOSDRV:        .res    1              ; the drive the BIOS has selected + 1, 0 none
AGE:            .res    2              ; the buffers' clock, for least recently used
SMODE:          .res    1              ; FIND's mode
FCBN:           .res    1              ; the FCB's bytes FCBIN read, 0 none
EXMASK:         .res    1              ; ~EXM & $1F: an extent's group
PEX3:           .res    1              ; SPAT's extent as a hash entry has it, and the mask
MASK3:          .res    1
PHASH:          .res    2              ; SPAT's name's hash
SZ:             .res    3              ; a size or a count, 24 bits
RR:             .res    3              ; a random record

.segment "SYSBSS"
FC:              .res    FCB_RAND       ; the program's FCB, while a function works on it
SPAT:           .res    14             ; FIND's pattern: the user, the name, EX, S2
SSAVE:          .res    14             ; function 17's, for 18
SSMODE:         .res    1
SSDRV:          .res    1
SSIDX:          .res    2              ; the entry 18 goes on from
SSON:           .res    1              ; 1 while there is one to go on with
DTAB:           .res    NSLOT * CD_LEN ; each drive's CD, from its login
LOGVEC:         .res    2              ; the logged-in drives, bit n drive n
ROVEC:          .res    2              ; the read-only drives
FCBP:           .res    2              ; the program's FCB
BCB_DRV:        .res    NBCB           ; each buffer's drive + 1, 0 empty
BCB_LO:         .res    NBCB           ; its sector
BCB_HI:         .res    NBCB
BCB_DIRTY:      .res    NBCB           ; 1: changed since read or written
BCB_AGEL:       .res    NBCB           ; when it was last used (AGE)
BCB_AGEH:       .res    NBCB

.segment "BDOS"

BDOSENT:        STA     PA
                STX     PX
                STA     PP
                STX     PP + 1
                STY     FUNC
                STZ     FCBN
                LDA     SCB_DISK
                STA     EDRV
                CPY     #NFUNC
                BCS     NOFUNC
                LDX     FIDX,Y
                BMI     NOFUNC
                LDA     PA
                JMP     (FJUMPS,X)
NOFUNC:
RETFF:          LDA     #$FF
                TAX
                RTS

; Each function's handler: FIDX has its offset in FJUMPS, $FF for none.
FJUMPS:
                .word   F_TERM, F_READ, F_WRITE, F_RAWIO
                .word   F_STRING, F_LINE, F_STAT, F_VERSION
                .word   F_RESET, F_SELECT, F_OPEN, F_CLOSE
                .word   F_SFIRST, F_SNEXT, F_DELETE, F_SREAD
                .word   F_SWRITE, F_MAKE, F_RENAME, F_LOGIN
                .word   F_DRIVE, F_DMA, F_ALLOC, F_SETRO
                .word   F_ROVEC, F_ATTRIB, F_DPB, F_USER
                .word   F_RREAD, F_RWRITE, F_SIZE, F_SETRAND
                .word   F_DRESET, F_RWRITEZ, F_LOCK, F_ERRMODE
                .word   F_FREE, F_FLUSH, F_BIOS, F_OODOS
                .word   F_FREEBLK, F_TRUNC, F_TSET, F_TGET
FIDX:
                .byte   0, 2, 4, $FF, $FF, $FF, 6, $FF, $FF, 8 ; 0-9
                .byte   10, 12, 14, 16, 18, 20, 22, 24, 26, 28 ; 10-19
                .byte   30, 32, 34, 36, 38, 40, 42, 44, 46, 48 ; 20-29
                .byte   50, 52, 54, 56, 58, 60, 62, 64, $FF, $FF ; 30-39
                .byte   66, $FF, 68, 68, $FF, 70, 72, $FF, 74, $FF ; 40-49
                .byte   76, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF ; 50-59
                .byte   $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF ; 60-69
                .byte   $FF, $FF, 78, $FF, $FF, $FF, $FF, $FF, $FF, $FF ; 70-79
                .byte   $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF ; 80-89
                .byte   $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, 80, 82 ; 90-99
                .byte   $FF, $FF, $FF, $FF, 84, 86 ; 100-105
.assert * - FIDX = NFUNC, error, "FIDX has every function to 105"

F_TERM:         JMP     WARM

F_READ:         JSR     B_CONIN
                PHA
                CMP     #' '
                BCS     @echo
                CMP     #ASCII_CR
                BEQ     @echo
                CMP     #ASCII_LF
                BEQ     @echo
                CMP     #ASCII_BS
                BEQ     @echo
                CMP     #ASCII_TAB
                BNE     @done
@echo:          JSR     OUT
@done:          PLA
                LDX     #0
                RTS

F_WRITE:        JSR     OUT
                LDA     #0
                TAX
                RTS

F_RAWIO:        CMP     #$FF
                BNE     @status
                JSR     B_CONST
                BEQ     ZERO
                JSR     B_CONIN
                LDX     #0
                RTS
@status:        CMP     #$FE
                BNE     @input
                JSR     B_CONST
                LDX     #0
                RTS
@input:         CMP     #$FD
                BNE     @output
                JSR     B_CONIN
                LDX     #0
                RTS
@output:        JSR     B_CONOUT
ZERO:           LDA     #0
                TAX
                RTS

F_STRING:       JSR     PGET
                CMP     SCB_DELIM
                BEQ     ZERO
                JSR     OUT
                BRA     F_STRING

F_STAT:         JSR     B_CONST
                BEQ     ZERO
                LDA     #1
                LDX     #0
                RTS

F_VERSION:      LDA     #$31
                LDX     #0
                RTS

F_DRIVE:        LDA     SCB_DISK
                LDX     #0
                RTS

; A line, with what CP/M's line editing has that a terminal needs.
F_LINE:         LDA     PA
                ORA     PX
                BNE     @buffer
                LDA     SCB_DMA        ; 0: the DMA address
                STA     PP
                LDA     SCB_DMA + 1
                STA     PP + 1
@buffer:        JSR     PGET           ; +0 its size; PP is +1, the count
                STA     SIZE
                STZ     COUNT
@key:           JSR     B_CONIN
                CMP     #ASCII_CR
                BEQ     @end
                CMP     #ASCII_LF
                BEQ     @end
                CMP     #ASCII_BS
                BEQ     @back
                CMP     #ASCII_DEL
                BEQ     @back
                CMP     #ASCII_CTLX
                BEQ     @cancel
                CMP     #ASCII_CTLC
                BNE     @char
                LDX     COUNT
                BNE     @key
                JMP     WARM           ; ^C at the start: the warm boot
@char:          CMP     #' '
                BCC     @key           ; other control characters: nothing
                LDX     COUNT
                CPX     SIZE
                BCS     @key           ; full
                PHA
                JSR     OUT
                PLA
                LDY     COUNT          ; at +2 + COUNT: PP + 1 + COUNT
                INY
                JSR     PPUTY
                INC     COUNT
                BRA     @key
@back:          LDA     COUNT
                BEQ     @key
                JSR     RUBOUT
                DEC     COUNT
                BRA     @key
@cancel:        LDA     COUNT
                BEQ     @key
                JSR     RUBOUT
                DEC     COUNT
                BRA     @cancel
@end:           LDA     #ASCII_CR
                JSR     OUT
                LDA     COUNT
                LDY     #0
                JSR     PPUTY
                LDA     #0
                TAX
                RTS

RUBOUT:         LDA     #ASCII_BS
                JSR     OUT
                LDA     #' '
                JSR     OUT
                LDA     #ASCII_BS
                JMP     OUT

; The BIOS for the program: its parameter block in, a JSR through the jump
; table, A and X out.
F_BIOS:         JSR     PGET
                CMP     #BIOS::BOOT
                BEQ     @refused
                CMP     #BIOS::COUNT
                BCC     @known
@refused:       JMP     NOFUNC
@known:         STA     BIOSV          ; $F800 + 3 x the function
                ASL     A
                ADC     BIOSV
                ADC     #<COMMON
                STA     BIOSV
                LDA     #>COMMON
                ADC     #0
                STA     BIOSV + 1
                JSR     PGET
                STA     PA
                JSR     PGET
                STA     PX
                JSR     PGET
                PHA                    ; Y
                LDX     #0
@words:         JSR     PGET
                STA     BW1,X
                INX
                CPX     #4
                BNE     @words
                PLY
                LDA     PA
                LDX     PX
                JMP     (BIOSV)        ; its RTS is ours

F_OODOS:        LDX     #0
@copy:          LDA     VERSIONS,X
                JSR     PPUT
                INX
                CPX     #VERSIONS_LEN
                BNE     @copy
                LDA     #<FARCALL
                LDX     #>FARCALL
                RTS
VERSIONS:       .byte   OODOS_MAJOR, OODOS_MINOR, BIOS_MAJOR, BIOS_MINOR
                .word   MEM_FITTED, MEM_PROGRAMS
                .word   FARCALL
VERSIONS_LEN    = * - VERSIONS

F_TSET:         LDX     #0
@dat:           JSR     PGET
                STA     SCB_DATE,X
                INX
                CPX     #4
                BNE     @dat
                STZ     SCB_SEC
                LDA     #$FF
                JSR     B_TIME
                JMP     ZERO

F_TGET:         LDA     #0
                JSR     B_TIME
                BCC     @time
                JMP     NOFUNC         ; past 2155
@time:          LDX     #0
@dat:           LDA     SCB_DATE,X
                JSR     PPUT
                INX
                CPX     #4
                BNE     @dat
                LDA     SCB_SEC
                LDX     #0
                RTS

; The console's output, keeping the SCB's column: a tab to the next eighth
; column, as CP/M's.
OUT:            CMP     #ASCII_TAB
                BNE     @char
@tab:           LDA     #' '
                JSR     @char
                LDA     SCB_COLUMN
                AND     #7
                BNE     @tab
                RTS
@char:          PHA
                CMP     #' '
                BCS     @print
                CMP     #ASCII_CR
                BNE     @bs
                STZ     SCB_COLUMN
                BRA     @out
@bs:            CMP     #ASCII_BS
                BNE     @out
                LDA     SCB_COLUMN
                BEQ     @out
                DEC     SCB_COLUMN
                BRA     @out
@print:         INC     SCB_COLUMN
@out:           PLA
                JMP     B_CONOUT

; ---------------------------------------------------------------------------
; Errors and returns

; A function's result, the stack back to BDOSENT's frame (the stub's JSR
; left S at $FD), from however deep: A, X 0 (RETA); A and X (RETAX); 0
; (RET0).
RET0:           LDA     #0
RETA:           LDX     #0
RETAX:          STA     T0
                STX     T1
                LDX     #$FD
                TXS
                LDA     T0
                LDX     T1
                RTS

; The program's memory refused (the I/O hole, flash): A = X = $FF.
ABORT:          LDA     #$FF
                TAX
                BRA     RETAX

; An error, CP/M 3's code in A, on drive EDRV: as the error mode says.
DERR:           STA     ECODE
                LDA     SCB_ERRMODE
                CMP     #$FF
                BEQ     @back
                JSR     ESAY
                LDA     SCB_ERRMODE
                CMP     #$FE
                BEQ     @back
                JMP     WARM           ; the default: the program ends
@back:          LDA     #$FF
                LDX     ECODE
                BRA     RETAX

; "OODOS Error On A: Disk I/O", then "BDOS Function = 15 File = NAME    .TYP",
; as CP/M 3 lays its messages out.
ESAY:           LDX     #0
                JSR     SAYX           ; CR LF "OODOS Error On "
                LDA     EDRV
                AND     #$0F
                CLC
                ADC     #'A'
                JSR     OUT
                LDA     #':'
                JSR     OUT
                LDA     #' '
                JSR     OUT
                LDY     #ENAMES_LEN - 1
@code:          LDA     ECODE          ; the message for the code: its offset in ESTR
                CMP     ECODES,Y
                BEQ     @known
                DEY
                BPL     @code
                LDY     #0             ; none other is made; Disk I/O if it were
@known:         LDX     ENAMES,Y
                JSR     SAYX
                LDX     #EFUNC - ESTR
                JSR     SAYX           ; CR LF "BDOS Function = "
                LDA     FUNC
                JSR     DECOUT
                LDA     FCBN
                BEQ     @end
                LDX     #EFILE - ESTR
                JSR     SAYX           ; " File = "
                LDX     #FCB_NAME
@name:          LDA     FC,X
                AND     #$7F
                PHX
                JSR     OUT
                PLX
                INX
                CPX     #FCB_T1
                BNE     @dot
                LDA     #'.'
                PHX
                JSR     OUT
                PLX
@dot:           CPX     #FCB_EX
                BNE     @name
@end:           LDA     #ASCII_CR
                JSR     OUT
                LDA     #ASCII_LF
                JMP     OUT

; ESTR's string at X, to its 0.
SAYX:           LDA     ESTR,X
                BEQ     @done
                PHX
                JSR     OUT
                PLX
                INX
                BRA     SAYX
@done:          RTS

; A in decimal, without leading zeros.
DECOUT:         LDX     #0
@hundreds:      CMP     #100
                BCC     @hdone
                SBC     #100
                INX
                BRA     @hundreds
@hdone:         STX     T2
                LDX     #0
@tens:          CMP     #10
                BCC     @tdone
                SBC     #10
                INX
                BRA     @tens
@tdone:         STX     T3
                PHA                    ; the ones
                LDA     T2
                BEQ     @nohundreds
                ORA     #'0'
                JSR     OUT
                LDA     T3             ; and the tens, 0 or not
                BRA     @tensout
@nohundreds:    LDA     T3
                BEQ     @ones
@tensout:       ORA     #'0'
                JSR     OUT
@ones:          PLA
                ORA     #'0'
                JMP     OUT

ESTR:           .byte   ASCII_CR, ASCII_LF, "OODOS Error On ", 0
EIO:            .byte   "Disk I/O", 0
ERO:            .byte   "Read Only", 0
EROF:           .byte   "File R/O", 0
ESEL:           .byte   "Invalid Drive", 0
EEXIST:         .byte   "File Exists", 0
EWILD:          .byte   "? in Filename", 0
EFUNC:          .byte   ASCII_CR, ASCII_LF, "BDOS Function = ", 0
EFILE:          .byte   " File = ", 0
.assert * - ESTR < 256, error, "ESTR's offsets are a byte"
ECODES:         .byte   ER_IO, ER_RODISK, ER_ROFILE, ER_SELECT, ER_EXISTS, ER_WILD
ENAMES:         .byte   EIO - ESTR, ERO - ESTR, EROF - ESTR, ESEL - ESTR, EEXIST - ESTR, EWILD - ESTR
ENAMES_LEN      = * - ENAMES

; ---------------------------------------------------------------------------
; The program's FCB and memory

; FCBIN: A bytes of the FCB at PP (the parameter) into F; FCBOUT writes them
; back. FCBSEL: FCBIN, then the FCB's drive selected (bdosdisk.s).
FCBSEL:         JSR     FCBIN
                JSR     FCBDRV
                JMP     SELECT
FCBIN:          STA     FCBN
                LDA     PP
                STA     FCBP
                STA     BW1
                LDA     PP + 1
                STA     FCBP + 1
                STA     BW1 + 1
                LDA     #<FC
                STA     BW2
                LDA     #>FC
                STA     BW2 + 1
                LDA     FCBN
                JMP     PIN
FCBOUT:         LDA     #<FC
                STA     BW1
                LDA     #>FC
                STA     BW1 + 1
                LDA     FCBP
                STA     BW2
                LDA     FCBP + 1
                STA     BW2 + 1
                LDA     FCBN
                JMP     POUT

; A = the FCB's drive: its DR, 0 the current one. Past P: Invalid Drive.
FCBDRV:         LDA     FC + FCB_DR
                BEQ     @current
                DEC     A
                CMP     #16
                BCC     @done
                STA     EDRV
                LDA     #ER_SELECT
                JMP     DERR
@current:       LDA     SCB_DISK
@done:          RTS

; Carry set if F's name or type has a '?'.
ISWILD:         LDX     #FCB_NAME
@char:          LDA     FC,X
                AND     #$7F
                CMP     #'?'
                BEQ     @wild
                INX
                CPX     #FCB_EX
                BNE     @char
                CLC
                RTS
@wild:          SEC
                RTS

; PIN: A bytes from the program's BW1 to the system's BW2 (bank 0: W2 the
; block the BDOS has there); POUT from the system's BW1 to the program's
; BW2. By the BIOS's XMOVE and MOVE; memory it refuses ends the function.
PIN:            PHA
                LDA     #BANK_PROG
                LDX     #BANK_SYS
                BRA     PCOPY
POUT:           PHA
                LDA     #BANK_SYS
                LDX     #BANK_PROG
PCOPY:          JSR     B_XMOVE
                PLA
                LDX     #0
                JSR     B_MOVE
                CMP     #$FF
                BNE     @done
                CPX     #$FF
                BNE     @done
                JMP     ABORT
@done:          RTS

; A = drive n's bit in a vector, X its byte (0 or 1).
DRVBIT:         PHA
                AND     #7
                TAX
                LDA     #1
@shift:         DEX
                BMI     @done
                ASL     A
                BRA     @shift
@done:          TAY
                PLA
                AND     #8
                LSR     A
                LSR     A
                LSR     A
                TAX
                TYA
                RTS

; The BIOS, through the jump table (common.s), which keeps W2
B_CONST:        JMP     JT(BIOS::CONST)
B_CONIN:        JMP     JT(BIOS::CONIN)
B_CONOUT:       JMP     JT(BIOS::CONOUT)
B_TIME:         JMP     JT(BIOS::TIME)
B_SELDSK:       JMP     JT(BIOS::SELDSK)
B_SETTRK:       JMP     JT(BIOS::SETTRK)
B_SETSEC:       JMP     JT(BIOS::SETSEC)
B_SETDMA:       JMP     JT(BIOS::SETDMA)
B_READ:         JMP     JT(BIOS::READ)
B_WRITE:        JMP     JT(BIOS::WRITE)
B_MOVE:         JMP     JT(BIOS::MOVE)
B_SETBNK:       JMP     JT(BIOS::SETBNK)
B_XMOVE:        JMP     JT(BIOS::XMOVE)

; A = the program's byte at PP, then PP + 1. Keeps X and Y.
PGET:           PHX
                JSR     PMAP
                LDA     (TP)
                BRA     PDONE

; A to the program's byte at PP, then PP + 1. Keeps X and Y.
PPUT:           PHX
                PHA
                JSR     PMAP
                PLA
                STA     (TP)
PDONE:          LDX     #BIOS_BLOCK
                STX     NB_WIN2
                INC     PP
                BNE     @done
                INC     PP + 1
@done:          PLX
                RTS

; A to the program's byte at PP + Y, PP left as it is. Keeps X and Y.
PPUTY:          PHX
                PHA
                PHY
                LDA     PP
                PHA
                LDA     PP + 1
                PHA
                TYA
                CLC
                ADC     PP
                STA     PP
                BCC     @put
                INC     PP + 1
@put:           TSX                    ; the byte, from under the three pushed after it
                LDA     $0104,X
                JSR     PPUT
                PLA
                STA     PP + 1
                PLA
                STA     PP
                PLY
                PLA
                PLX
                RTS

; W2 on the block the program's window had at PP; TP its address there. An
; address in the I/O hole ends the function: A = X = $FF, from BDOSENT's
; own frame (the stub's JSR left S at $FD), the BIOS still in W2.
PMAP:           LDA     PP + 1
                CMP     #>CODE_WINDOW
                BCS     @memory
                CMP     #$C0
                BCC     @memory
                LDX     #$FD
                TXS
                JMP     NOFUNC
@memory:        ROL     A              ; the window: the top 2 bits
                ROL     A
                ROL     A
                AND     #3
                TAX
                LDA     ST_WIN,X
                STA     NB_WIN2
                LDA     PP + 1
                AND     #$3F
                ORA     #$80
                STA     TP + 1
                LDA     PP
                STA     TP
                RTS

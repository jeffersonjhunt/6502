; OOLDR: OODOS's loader, /BOOT.BIN on an OODOS card (hw/oodos.md, "The
; system, phase 2"). The ROM BIOS's C80CR loads it at $1000 and calls it,
; with W0-W2 on blocks 0-2 and W3 still the ROM BIOS (block 63), so it
; prints with ECHO ($C80F, which keeps A, X and Y), and a failure says what
; and returns, to the monitor. It:
;
;   1. reads /OODOS/DRIVES.TXT, its size from LIST of /OODOS, its bytes
;      through unit 1 mounted for the purpose, and checks all of it by the
;      grammar in hw/oodos.md ("Drives") before mounting anything: any error
;      by its line number;
;   2. unmounts units 2-7 that no line names, mounts each line's image as
;      its unit, read-only for RO, and refuses one that isn't 16,384 or
;      65,536 sectors (oodos-8m, oodos-32m), by the line's number, and two
;      drives the units table names the same file (an 8.3 alias, say);
;   3. finds OODOS.SYS in A:'s user 0 with the CP/M reader (../sys/cpmread.s),
;      checks its header (system.inc) and puts each segment at its physical
;      address through W1 and W2;
;   4. sets W1, W2, W3 and VEC to blocks 60, 61, 62 and 62 (W0 stays block
;      0), interrupts and decimal off, and jumps to the cold boot's entry.
;
; Everything it has is below $4000 (W0, block 0), so W1 and W2 are free to
; look at the segments' blocks. Zero page: $00-$1F (ldr.cfg). Every wait for
; the card is bounded: about a second, then SD TIMEOUT (the command
; abandoned by CTRL's RESET, units staying mounted) and back to the monitor.

.setcpu "65C02"
.include "backplane.inc"
.include "system.inc"
.import CR_INIT, CR_FIND, CR_SEEK, CR_READ, CR_RECORDS, CR_SDERR

ECHO            = $C80F
SD_UNMOUNT      = $A1                  ; UNIT detached (hw/design.md, "Mass storage registers")
MOUNT_RO        = $01                  ; MOUNT's flags: read-only
UNIT_MOUNTED    = $01                  ; IDENTIFY's units table, its flags
UNITS           = $40
UNIT_NAME       = 8                    ; a unit's file name, at +8...
UNIT_NAME_LEN   = 24                   ; ...24 bytes, NUL padded
MAX_PATH        = 509                  ; MOUNT's sector: the unit, the flags, the path, its NUL
DRIVES          = 7                    ; A-G: units 1-7
LIST_ENTRY      = 64
LIST_NAME       = 8
SYS_BLOCKS_LO   = $F0000               ; blocks 60-62
SYS_BLOCKS_HI   = $FC000
SYS_ZP_LO       = $7C000               ; block 31
SYS_ZP_HI       = $80000
SEG_MAX         = $4000

; DRIVES.TXT's parser, a state a byte (hw/oodos.md's table of lines)
.enum
        P_START                        ; the line's start, blanks
        P_LETTER                       ; a drive letter: a colon next
        P_COLON                        ; the colon: a blank next
        P_BLANK                        ; blanks before the path
        P_PATH
        P_AFTER                        ; blanks after the path
        P_R                            ; R after them
        P_RO                           ; RO
        P_AFTER_RO                     ; blanks after RO
        P_NOTE                         ; a comment line
        P_DRIVE_NOTE                   ; a comment after a drive line
        P_CR                           ; a CR: LF must follow
.endenum

.segment "ZEROPAGE"
ptr:            .res    2              ; a message, a name, a path
dst:            .res    2              ; where the next byte goes
src:            .res    2
left:           .res    3              ; DRIVES.TXT's bytes still to read; a segment's
num:            .res    2              ; a number to print
p1:             .res    2              ; SAME_PATH's
p2:             .res    2

.segment "BSS"
stack:          .res    1              ; S at entry, for failing from anywhere
state:          .res    1
before_cr:      .res    1              ; the state a CR came in
line:           .res    2
drive:          .res    1              ; this line's: 0-6
plen:           .res    2              ; its path's length
ro:             .res    1
lba:            .res    2              ; DRIVES.TXT's next sector
count:          .res    2              ; bytes of this sector to parse
named:          .res    DRIVES         ; nonzero: the drive has a line
lines:          .res    DRIVES * 2     ; ...and this is its number
entry:          .res    2              ; OODOS.SYS's cold boot
eph:            .res    3              ; ...as a physical address in block 62
bigsize:        .res    1              ; DRIVES.TXT's size's top byte
nlen:           .res    1              ; the path's name so far: its length, to 3...
alldots:        .res    1              ; ...bit 7: dots only
tmo:            .res    3              ; a wait's passes left
seg:            .res    1              ; a segment's index
other:          .res    1
starts:         .res    SYS_MAX_SEGMENTS * 3 ; each segment's first byte (physical)...
ends:           .res    SYS_MAX_SEGMENTS * 3 ; ...and the byte after its last
buf:            .res    512            ; a sector: LIST's, IDENTIFY's, DRIVES.TXT's
line_buf:       .res    512            ; this line's MOUNT sector: unit, flags, path
slots:          .res    DRIVES * 512   ; each drive's MOUNT sector
header:         .res    128
record:         .res    128

.segment "CODE"

START:          CLD
                TSX
                STX     stack
                LDA     #<MBANNER
                LDX     #>MBANNER
                JSR     SAY

; 1. DRIVES.TXT: its size, from LIST of /OODOS.
                JSR     CLEARBUF
                LDX     #0
@dir:           LDA     OODOS_DIR,X
                STA     buf,X
                INX
                CPX     #OODOS_DIR_LEN
                BNE     @dir
                STZ     named          ; found it (named is cleared again below)
                JSR     IDLE
                STZ     SD_CTRL
                LDA     #SD_LIST
                STA     SD_CMD
                JSR     PUTBUF
                BCS     @listed        ; it ended already: why, below
@list:          JSR     GETBUF
                BCS     @listed
                LDA     #<buf
                STA     src
                LDA     #>buf
                STA     src+1
                LDA     #512 / LIST_ENTRY
                STA     other
@entry:         JSR     IS_DRIVES
                LDA     src
                CLC
                ADC     #LIST_ENTRY
                STA     src
                BCC     @more
                INC     src+1
@more:          DEC     other
                BNE     @entry
                BRA     @list
@listed:        JSR     DONE
                BCC     @known
                LDA     SD_ERROR       ; no /OODOS is "not on the card" too
                CMP     #SD_NOT_FOUND
                BEQ     @nodrives
                JMP     SDFAIL
@known:         LDA     named
                BEQ     @nodrives
                LDA     bigsize
                BEQ     @mount
                LDA     #<MTOOBIG
                LDX     #>MTOOBIG
                JMP     FAIL
@nodrives:      LDA     #<MNODRIVES
                LDX     #>MNODRIVES
                JMP     FAIL

; Its bytes, through unit 1.
@mount:         JSR     CLEARBUF
                LDA     #1
                STA     buf
                LDA     #MOUNT_RO
                STA     buf + 1
                LDX     #0
@path:          LDA     DRIVES_PATH,X
                STA     buf + 2,X
                BEQ     @send
                INX
                BRA     @path
@send:          LDA     #<buf
                LDX     #>buf
                JSR     MOUNT
                BCC     @parse
                JMP     SDFAIL

@parse:         LDX     #DRIVES - 1
@clear:         STZ     named,X
                DEX
                BPL     @clear
                STZ     lba
                STZ     lba+1
                LDA     #1
                STA     line
                STZ     line+1
                LDA     #P_START
                STA     state
@sector:        LDA     left
                ORA     left+1
                ORA     left+2
                BEQ     @eof
                LDA     lba
                LDX     lba+1
                JSR     READ1
                BCC     @got
                JMP     SDFAIL
@got:           LDA     left+1         ; count = the sector's bytes, or what's left
                CMP     #2
                LDA     left+2
                SBC     #0
                BCC     @part
                STZ     count
                LDA     #2
                STA     count+1
                BRA     @take
@part:          LDA     left
                STA     count
                LDA     left+1
                STA     count+1
@take:          SEC                    ; left -= count
                LDA     left
                SBC     count
                STA     left
                LDA     left+1
                SBC     count+1
                STA     left+1
                LDA     left+2
                SBC     #0
                STA     left+2
                LDA     #<buf
                STA     src
                LDA     #>buf
                STA     src+1
                LDA     lba            ; a byte order mark first is skipped
                ORA     lba+1
                BNE     @bytes
                JSR     SKIP_BOM
@bytes:         LDA     count
                ORA     count+1
                BEQ     @next
                LDA     (src)
                JSR     PARSE
                INC     src
                BNE     @count
                INC     src+1
@count:         LDA     count
                BNE     @lo
                DEC     count+1
@lo:            DEC     count
                BRA     @bytes
@next:          INC     lba
                BNE     @again
                INC     lba+1
@again:         JMP     @sector
@eof:           LDA     state          ; the last line, with no ending
                CMP     #P_CR
                BNE     @last
                JMP     CR_ERROR       ; a CR at the end: not before LF
@last:          JSR     EOL
                LDA     named          ; A:
                BNE     UNITS2
                LDA     #<MNOA
                LDX     #>MNOA
                JMP     FAIL

; 2. The drives: units no line names unmounted, then each mounted and its size checked.
UNITS2:         JSR     IDLE
                STZ     SD_CTRL
                LDA     #SD_IDENTIFY
                STA     SD_CMD
                JSR     GETBUF
                BCS     @sd
                JSR     DONE
                BCS     @sd
                LDX     #1             ; units 2-7: X is the drive
                LDY     #32            ; and Y its offset in the table
@unit:          LDA     named,X
                BNE     @skip
                LDA     buf + UNITS + 4,Y
                AND     #UNIT_MOUNTED
                BEQ     @skip
                INX
                STX     SD_UNIT
                DEX
                LDA     #SD_UNMOUNT
                STA     SD_CMD
                JSR     DONE
                BCS     @sd
@skip:          TYA
                CLC
                ADC     #32
                TAY
                INX
                CPX     #DRIVES
                BNE     @unit

                STZ     seg            ; the drive
@drive:         LDX     seg
                LDA     named,X
                BEQ     @nextd
                TXA                    ; its slot: unit, flags, path
                ASL
                ADC     #>slots
                TAX
                LDA     #<slots
                JSR     MOUNT
                BCC     @size
                LDA     SD_ERROR
                CMP     #SD_NOT_FOUND
                BNE     @sd
                JSR     DRIVE_SAY
                JSR     SAY_PATH
                LDA     #<MNOTFOUND
                LDX     #>MNOTFOUND
                JMP     FAIL
@size:          LDA     seg
                INC
                JSR     CR_INIT
                BCC     @nextd
@cr:            JMP     CRFAIL         ; drive seg's: the size, say
@sd:            JMP     SDFAIL
@nextd:         INC     seg
                LDA     seg
                CMP     #DRIVES
                BNE     @drive
                JSR     SAMEFILE

; 3. OODOS.SYS, on A:.
SYS_A:          STZ     seg            ; A:, for CRFAIL
                LDA     #1
                JSR     CR_INIT
                BCS     @cr
                LDA     #<SYS_NAME
                LDX     #>SYS_NAME
                JSR     CR_FIND
                BCC     @found
                CMP     #CRE_NOFILE
                BNE     @cr
                LDA     #<MNOSYS
                LDX     #>MNOSYS
                JMP     FAIL
@found:         LDA     #<header
                LDX     #>header
                JSR     CR_READ
                BCC     CHECK
                CMP     #CRE_EOF
                BNE     @cr
                LDA     #<MEMPTY
                LDX     #>MEMPTY
                JMP     FAIL
@cr:            JMP     CRFAIL

; The header, all of it, before anything is loaded.
CHECK:          LDX     #3
@magic:         LDA     header + SH_MAGIC,X
                CMP     MAGIC,X
                BNE     @badmagic
                DEX
                BPL     @magic
                LDA     header + SH_MAJOR
                CMP     #SYS_MAJOR
                BEQ     @count
                JSR     SYS_SAY
                LDA     #<MVERSION
                LDX     #>MVERSION
                JSR     SAY
                LDA     header + SH_MAJOR
                JSR     SAY_BYTE
                LDA     #<MVERSION2
                LDX     #>MVERSION2
                JMP     FAIL
@badmagic:      JSR     SYS_SAY
                LDA     #<MMAGIC
                LDX     #>MMAGIC
                JMP     FAIL
@count:         LDA     header + SH_COUNT
                BEQ     @badcount
                CMP     #SYS_MAX_SEGMENTS + 1
                BCC     @entry
@badcount:      JSR     SYS_SAY
                LDA     header + SH_COUNT
                JSR     SAY_BYTE
                LDA     #<MCOUNT
                LDX     #>MCOUNT
                JMP     FAIL
@entry:         LDA     header + SH_ENTRY
                STA     entry
                LDA     header + SH_ENTRY + 1
                STA     entry+1
                CMP     #>CODE_WINDOW  ; $C800-$FFFF: block 62 through W3, past the I/O hole
                BCS     SEGMENTS
                JSR     SYS_SAY
                LDA     #<MENTRY
                LDX     #>MENTRY
                JMP     FAIL

; Each segment: at most 16 KB, inside block 31 or blocks 60-62, inside the
; file, and clear of the ones before it.
SEGMENTS:       STZ     seg
                LDA     #<(header + SH_SEGMENTS)
                STA     src
                LDA     #>(header + SH_SEGMENTS)
                STA     src+1
@one:           LDY     #SS_LENGTH + 1 ; the length
                LDA     (src),Y
                CMP     #>SEG_MAX
                BCC     @short
                BNE     @long
                DEY
                LDA     (src),Y
                BEQ     @short         ; 16 KB exactly
@long:          LDA     #<MLONG
                LDX     #>MLONG
                JMP     SEG_FAIL
@empty:         LDA     #<MEMPTYSEG
                LDX     #>MEMPTYSEG
                JMP     SEG_FAIL
@inheader:      LDA     #<MRECORD0
                LDX     #>MRECORD0
                JMP     SEG_FAIL
@short:         LDY     #SS_LENGTH     ; not 0
                LDA     (src),Y
                INY
                ORA     (src),Y
                BEQ     @empty
                LDY     #SS_RECORD     ; record 0 is the header
                LDA     (src),Y
                INY
                ORA     (src),Y
                BEQ     @inheader
                LDX     seg            ; its start and end
                LDY     #SS_ADDR
                LDA     (src),Y
                STA     starts,X
                CLC
                LDY     #SS_LENGTH
                ADC     (src),Y
                STA     ends,X
                LDY     #SS_ADDR + 1
                LDA     (src),Y
                STA     starts + SYS_MAX_SEGMENTS,X
                LDY     #SS_LENGTH + 1
                ADC     (src),Y
                STA     ends + SYS_MAX_SEGMENTS,X
                LDY     #SS_ADDR + 2
                LDA     (src),Y
                STA     starts + 2 * SYS_MAX_SEGMENTS,X
                ADC     #0
                STA     ends + 2 * SYS_MAX_SEGMENTS,X
                LDA     #<SYS_ZP_LO    ; block 31...
                LDY     #>SYS_ZP_LO
                STY     num
                LDY     #^SYS_ZP_LO
                JSR     START_GE
                BCC     @blocks
                LDA     #<SYS_ZP_HI
                LDY     #>SYS_ZP_HI
                STY     num
                LDY     #^SYS_ZP_HI
                JSR     END_LE
                BCS     @placed
@blocks:        LDA     #<SYS_BLOCKS_LO ; ...or blocks 60-62
                LDY     #>SYS_BLOCKS_LO
                STY     num
                LDY     #^SYS_BLOCKS_LO
                JSR     START_GE
                BCC     @outside
                LDA     #<SYS_BLOCKS_HI
                LDY     #>SYS_BLOCKS_HI
                STY     num
                LDY     #^SYS_BLOCKS_HI
                JSR     END_LE
                BCS     @placed
@outside:       LDA     #<MOUTSIDE
                LDX     #>MOUTSIDE
                JMP     SEG_FAIL
@placed:        LDY     #SS_LENGTH     ; inside the file: record + ceil(length / 128)
                LDA     (src),Y        ; <= its records
                CLC
                ADC     #127
                STA     left
                INY
                LDA     (src),Y
                ADC     #0
                STA     left+1
                ASL     left           ; (length + 127) >> 7, in left+1: at most 128
                ROL     left+1
                LDY     #SS_RECORD
                LDA     (src),Y
                CLC
                ADC     left+1
                STA     num
                INY
                LDA     (src),Y
                ADC     #0
                STA     num+1
                BCS     @past
                LDA     CR_RECORDS
                CMP     num
                LDA     CR_RECORDS+1
                SBC     num+1
                BCS     @clear
@past:          LDA     #<MPAST
                LDX     #>MPAST
                JMP     SEG_FAIL
@clear:         LDX     seg            ; and no overlap: start < the other's end and
                BEQ     @next          ; the other's start < end
                DEX
@other:         STX     other
                LDY     seg
                JSR     OVERLAP
                BCS     @overlap
                LDX     other
                DEX
                BPL     @other
@next:          LDA     src
                CLC
                ADC     #SS_LEN
                STA     src
                BCC     @inc
                INC     src+1
@inc:           INC     seg
                LDA     seg
                CMP     header + SH_COUNT
                BEQ     INSIDE
                JMP     @one
@overlap:       JSR     SYS_SAY
                LDA     #<MSEGMENTS
                LDX     #>MSEGMENTS
                JSR     SAY
                LDA     other
                INC
                JSR     SAY_BYTE
                LDA     #<MAND
                LDX     #>MAND
                JSR     SAY
                LDA     seg
                INC
                JSR     SAY_BYTE
                LDA     #<MOVERLAP
                LDX     #>MOVERLAP
                JMP     FAIL

; The entry, in block 62 ($EC000 + the CPU address), must be in a segment.
INSIDE:         LDA     entry
                STA     eph
                LDA     entry+1
                SEC
                SBC     #$40
                STA     eph+1
                LDA     #$0F
                STA     eph+2
                LDX     #0
@seg:           LDA     eph            ; start <= it...
                CMP     starts,X
                LDA     eph+1
                SBC     starts + SYS_MAX_SEGMENTS,X
                LDA     eph+2
                SBC     starts + 2 * SYS_MAX_SEGMENTS,X
                BCC     @next
                LDA     eph            ; ...< end
                CMP     ends,X
                LDA     eph+1
                SBC     ends + SYS_MAX_SEGMENTS,X
                LDA     eph+2
                SBC     ends + 2 * SYS_MAX_SEGMENTS,X
                BCC     LOAD
@next:          INX
                CPX     header + SH_COUNT
                BNE     @seg
                JSR     SYS_SAY
                LDA     #<MNOENTRY
                LDX     #>MNOENTRY
                JMP     FAIL

; 4. Each segment to its place: W1 its first block, W2 the next, the record
; read through `record` so a last part record writes only its bytes.
LOAD:           STZ     seg
                LDA     #<(header + SH_SEGMENTS)
                STA     src
                LDA     #>(header + SH_SEGMENTS)
                STA     src+1
@one:           LDY     #SS_ADDR + 1   ; the block: address >> 14
                LDA     (src),Y
                STA     dst+1
                INY
                LDA     (src),Y
                ASL     dst+1
                ROL
                ASL     dst+1
                ROL
                STA     NB_WIN1
                INC
                STA     NB_WIN2
                LDY     #SS_ADDR       ; dst = $4000 + address & $3FFF
                LDA     (src),Y
                STA     dst
                INY
                LDA     (src),Y
                AND     #$3F
                ORA     #$40
                STA     dst+1
                LDY     #SS_LENGTH
                LDA     (src),Y
                STA     left
                INY
                LDA     (src),Y
                STA     left+1
                LDY     #SS_RECORD + 1
                LDA     (src),Y
                TAX
                DEY
                LDA     (src),Y
                JSR     CR_SEEK
@record:        LDA     left
                ORA     left+1
                BEQ     @next
                LDA     #<record
                LDX     #>record
                JSR     CR_READ
                BCC     @copy
                JMP     CRFAIL         ; CRE_EOF too: checked above, so the directory's wrong
@copy:          LDX     #128           ; this many: 128, or what's left
                LDA     left+1
                BNE     @n
                LDA     left
                CMP     #128
                BCS     @n
                TAX
@n:             STX     num
                LDY     #0
@byte:          LDA     record,Y
                STA     (dst),Y
                INY
                DEX
                BNE     @byte
                LDA     dst
                CLC
                ADC     num
                STA     dst
                BCC     @less
                INC     dst+1
@less:          SEC
                LDA     left
                SBC     num
                STA     left
                BCS     @record
                DEC     left+1
                BRA     @record
@next:          LDA     src
                CLC
                ADC     #SS_LEN
                STA     src
                BCC     @inc
                INC     src+1
@inc:           INC     seg
                LDA     seg
                CMP     header + SH_COUNT
                BEQ     GO
                JMP     @one
GO:

; 5. The system's windows, and its cold boot. W0 stays block 0, where entry is.
                SEI
                CLD
                LDA     #BDOS_BLOCK
                STA     NB_WIN1
                LDA     #BIOS_BLOCK
                STA     NB_WIN2
                LDA     #RES_BLOCK
                STA     NB_WIN3
                STA     NB_VEC
                JMP     (entry)

; ---- DRIVES.TXT ------------------------------------------------------------

; Is the LIST entry at src DRIVES.TXT, a file? Then named = 1 and left its size.
IS_DRIVES:      LDY     #4
                LDA     (src),Y
                AND     #$10           ; a directory
                BNE     @no
                LDY     #LIST_NAME
@cmp:           LDA     (src),Y
                JSR     UPPER
                CMP     DRIVES_NAME - LIST_NAME,Y
                BNE     @no
                INY
                CMP     #0
                BNE     @cmp
                LDY     #0             ; its size: the loader takes under 16 MB
                LDA     (src),Y
                STA     left
                INY
                LDA     (src),Y
                STA     left+1
                INY
                LDA     (src),Y
                STA     left+2
                INY
                LDA     (src),Y
                STA     bigsize        ; refused once LIST has ended
                LDA     #1
                STA     named
@no:            RTS

; At the file's start: skip a UTF-8 byte order mark, if the sector has one.
SKIP_BOM:       LDA     count+1
                BNE     @check
                LDA     count
                CMP     #3
                BCC     @no
@check:         LDY     #2
@cmp:           LDA     buf,Y
                CMP     BOM,Y
                BNE     @no
                DEY
                BPL     @cmp
                LDA     src
                CLC
                ADC     #3
                STA     src
                BCC     @count
                INC     src+1
@count:         SEC
                LDA     count
                SBC     #3
                STA     count
                BCS     @no
                DEC     count+1
@no:            RTS

; One byte of DRIVES.TXT, in A, by the state.
PARSE:          LDX     state
                CPX     #P_CR
                BEQ     @cr
                CMP     #$0D
                BNE     @lf
                STX     before_cr      ; LF must come next
                LDX     #P_CR
                STX     state
                RTS
@lf:            CMP     #$0A
                BNE     @state
                JMP     EOL
@cr:            CMP     #$0A
                BEQ     @crlf
                JMP     CR_ERROR
@crlf:          LDX     before_cr
                STX     state
                JMP     EOL
@state:         PHA
                TXA
                ASL
                TAX
                PLA
                JMP     (STATES,X)

STATES:         .word   S_START, S_LETTER, S_COLON, S_BLANK, S_PATH, S_AFTER, S_R
                .word   S_RO, S_AFTER_RO, S_NOTE, S_NOTE

S_START:        JSR     BLANK
                BEQ     @done
                CMP     #'#'
                BEQ     @note
                JSR     UPPER
                CMP     #'A'
                BCC     @bad
                CMP     #'G' + 1
                BCS     @bad
                SBC     #'A' - 1       ; carry clear: - 'A'
                STA     drive
                LDA     #P_LETTER
                STA     state
@done:          RTS
@note:          LDA     #P_NOTE
                STA     state
                RTS
@bad:           JMP     NOT_DRIVE

S_LETTER:       CMP     #':'
                BNE     @bad
                LDA     #P_COLON
                STA     state
                RTS
@bad:           JMP     NOT_DRIVE

S_COLON:        JSR     BLANK
                BNE     @bad
                LDA     #P_BLANK
                STA     state
                RTS
@bad:           LDA     #<MNOBLANK
                LDX     #>MNOBLANK
                JMP     LINE_FAIL

S_BLANK:        JSR     BLANK
                BEQ     @done
                CMP     #'/'
                BNE     @bad
                PHA                    ; a path begins: in line_buf from +2
                LDA     #<(line_buf + 2)
                STA     dst
                LDA     #>(line_buf + 2)
                STA     dst+1
                STZ     plen
                STZ     plen+1
                STZ     ro
                JSR     NEWNAME
                LDA     #P_PATH
                STA     state
                PLA
                JMP     ADD
@done:          RTS
@bad:           LDA     #<MNOSLASH
                LDX     #>MNOSLASH
                JMP     LINE_FAIL

S_PATH:         JSR     BLANK
                BEQ     @after
                CMP     #'#'
                BEQ     @note
                CMP     #$20
                BCC     @control
                CMP     #$7F
                BEQ     @control
                CMP     #'/'
                BNE     @char
                PHA                    ; the name before it ends
                JSR     ENDNAME
                JSR     NEWNAME
                PLA
                JMP     ADD
@char:          CMP     #'.'
                BEQ     @dot
                STZ     alldots
@dot:           LDX     nlen           ; counted to 3: enough to tell . and ..
                CPX     #3
                BCS     @add
                INC     nlen
@add:           JMP     ADD
@after:         LDA     #P_AFTER
                STA     state
                RTS
@note:          LDA     #P_DRIVE_NOTE
                STA     state
                RTS
@control:       LDA     #<MCONTROL
                LDX     #>MCONTROL
                JMP     LINE_FAIL

S_AFTER:        JSR     BLANK
                BEQ     @done
                CMP     #'#'
                BEQ     @note
                JSR     UPPER
                CMP     #'R'
                BNE     WORD
                LDA     #P_R
                STA     state
@done:          RTS
@note:          LDA     #P_DRIVE_NOTE
                STA     state
                RTS

S_R:            JSR     UPPER
                CMP     #'O'
                BNE     WORD
                LDA     #MOUNT_RO
                STA     ro
                LDA     #P_RO
                STA     state
                RTS

S_RO:           JSR     BLANK
                BEQ     @after
                CMP     #'#'
                BNE     WORD           ; ROM, say
                LDA     #P_DRIVE_NOTE
                STA     state
                RTS
@after:         LDA     #P_AFTER_RO
                STA     state
                RTS

S_AFTER_RO:     JSR     BLANK
                BEQ     @done
                CMP     #'#'
                BNE     WORD           ; a second RO, say
                LDA     #P_DRIVE_NOTE
                STA     state
@done:          RTS

S_NOTE:         RTS

WORD:           LDA     #<MWORD
                LDX     #>MWORD
                JMP     LINE_FAIL

NOT_DRIVE:      LDA     #<MNOTDRIVE
                LDX     #>MNOTDRIVE
                JMP     LINE_FAIL

CR_ERROR:       LDA     #<MCR
                LDX     #>MCR
                JMP     LINE_FAIL

; Z set if A is a space or a tab.
BLANK:          CMP     #' '
                BEQ     @yes
                CMP     #$09
@yes:           RTS

; A path's byte to line_buf, if there's room for it.
ADD:            STA     (dst)
                INC     dst
                BNE     @len
                INC     dst+1
@len:           INC     plen
                BNE     @check
                INC     plen+1
@check:         LDA     plen+1         ; over MAX_PATH?
                CMP     #>MAX_PATH
                BCC     @ok
                BNE     @long
                LDA     plen
                CMP     #<MAX_PATH + 1
                BCC     @ok
@long:          LDA     #<MLONGPATH
                LDX     #>MLONGPATH
                JMP     LINE_FAIL
@ok:            RTS

; The end of a line, by the state: a drive line is taken, a blank or
; comment one passes, anything else is an error.
EOL:            LDX     state
                LDA     #P_START
                STA     state
                CPX     #P_START
                BEQ     @next
                CPX     #P_NOTE
                BEQ     @next
                CPX     #P_LETTER
                BEQ     @letter
                CPX     #P_COLON
                BEQ     @colon
                CPX     #P_BLANK
                BEQ     @slash
                CPX     #P_R
                BEQ     @word          ; R alone
                JSR     TAKE
@next:          INC     line
                BNE     @done
                INC     line+1
@done:          RTS
@letter:        JMP     NOT_DRIVE
@word:          JMP     WORD
@colon:         LDA     #<MNOBLANK
                LDX     #>MNOBLANK
                JMP     LINE_FAIL
@slash:         LDA     #<MNOSLASH
                LDX     #>MNOSLASH
                JMP     LINE_FAIL

; A name of the path begins: after a /.
NEWNAME:        STZ     nlen
                LDA     #$80
                STA     alldots
                RTS

; A name of the path has ended: not empty (//), not . or .. (/./, /../),
; which would let one image be mounted by two paths.
ENDNAME:        LDA     nlen
                BEQ     @bad
                BIT     alldots
                BPL     @ok
                CMP     #3
                BCC     @bad
@ok:            RTS
@bad:           LDA     #<MNAME
                LDX     #>MNAME
                JMP     LINE_FAIL

; This line's drive: its path's last name, not named before, its path no
; other drive's (in any case), then into its slot.
TAKE:           JSR     ENDNAME
                LDA     #0             ; the path's NUL
                STA     (dst)
                LDX     drive
                LDA     named,X
                BEQ     @paths
                JSR     LINE_SAY
                LDA     drive
                CLC
                ADC     #'A'
                JSR     ECHO
                LDA     #<MTWICE
                LDX     #>MTWICE
                JMP     FAIL
@paths:         LDX     #DRIVES - 1
@path:          LDA     named,X
                BEQ     @nextp
                STX     other
                JSR     SAME_PATH
                BEQ     @same
                LDX     other
@nextp:         DEX
                BPL     @path
                LDA     drive          ; line_buf to the slot
                ASL
                CLC
                ADC     #>slots
                STA     dst+1
                LDA     #<slots
                STA     dst
                LDA     drive
                INC
                STA     line_buf       ; the unit
                LDA     ro
                STA     line_buf + 1
                LDY     #0
@copy:          LDA     line_buf,Y
                STA     (dst),Y
                INY
                BNE     @copy
                INC     dst+1
@copy2:         LDA     line_buf + 256,Y
                STA     (dst),Y
                INY
                BNE     @copy2
                LDX     drive
                LDA     #1
                STA     named,X
                TXA
                ASL
                TAX
                LDA     line
                STA     lines,X
                LDA     line+1
                STA     lines+1,X
                RTS
@same:          JSR     LINE_SAY
                LDA     #<(line_buf + 2)
                LDX     #>(line_buf + 2)
                JSR     SAY
                LDA     #<MSAME
                LDX     #>MSAME
                JSR     SAY
                LDA     other
                CLC
                ADC     #'A'
                JSR     ECHO
                LDA     #<MSAME2
                LDX     #>MSAME2
                JMP     FAIL

; Z set if line_buf's path is drive X's, compared in any case.
SAME_PATH:      TXA
                ASL
                CLC
                ADC     #>(slots + 2)
                STA     p1+1
                LDA     #<(slots + 2)
                STA     p1
                LDA     #<(line_buf + 2)
                STA     p2
                LDA     #>(line_buf + 2)
                STA     p2+1
@byte:          LDA     (p1)
                JSR     UPPER
                STA     num
                LDA     (p2)
                JSR     UPPER
                CMP     num
                BNE     @done          ; Z clear
                CMP     #0
                BEQ     @done          ; both ended: Z set
                INC     p1
                BNE     @p
                INC     p1+1
@p:             INC     p2
                BNE     @byte
                INC     p2+1
                BRA     @byte
@done:          RTS

; After mounting: no two drives on one file, as the units table names them
; (its first 24 bytes; the same name in two directories is refused too:
; the table is all the card says of a unit).
SAMEFILE:       JSR     IDLE
                STZ     SD_CTRL
                LDA     #SD_IDENTIFY
                STA     SD_CMD
                JSR     GETBUF
                BCS     @sd
                JSR     DONE
                BCS     @sd
                LDA     #1             ; drive seg against each drive other before it
                STA     seg
@x:             LDX     seg
                LDA     named,X
                BEQ     @nextx
                STZ     other
@y:             LDX     other
                LDA     named,X
                BEQ     @nexty
                LDA     seg
                JSR     UNITNAME
                LDA     p1
                STA     p2
                LDA     p1+1
                STA     p2+1
                LDA     other
                JSR     UNITNAME
                LDY     #UNIT_NAME_LEN - 1
@cmp:           LDA     (p1),Y
                CMP     (p2),Y
                BNE     @nexty
                DEY
                BPL     @cmp
                JSR     DRIVE_SAY
                JSR     SAY_PATH
                LDA     #<MSAME
                LDX     #>MSAME
                JSR     SAY
                LDA     other
                CLC
                ADC     #'A'
                JSR     ECHO
                LDA     #<MSAME2
                LDX     #>MSAME2
                JMP     FAIL
@nexty:         INC     other
                LDA     other
                CMP     seg
                BNE     @y
@nextx:         INC     seg
                LDA     seg
                CMP     #DRIVES
                BNE     @x
                RTS
@sd:            JMP     SDFAIL

; p1 = drive A's file name in IDENTIFY's units table, in buf.
UNITNAME:       ASL                    ; x 32: at most 192
                ASL
                ASL
                ASL
                ASL
                CLC
                ADC     #<(buf + UNITS + UNIT_NAME)
                STA     p1
                LDA     #>(buf + UNITS + UNIT_NAME)
                ADC     #0
                STA     p1+1
                RTS

UPPER:          CMP     #'a'
                BCC     @done
                CMP     #'z' + 1
                BCS     @done
                AND     #$DF
@done:          RTS

; ---- the card ---------------------------------------------------------------

; MOUNT the sector at A/X. Carry set if the card failed it.
MOUNT:          STA     ptr
                STX     ptr+1
                JSR     IDLE
                STZ     SD_CTRL
                LDA     #SD_MOUNT
                STA     SD_CMD
                LDY     #0             ; its sector, once DATA takes it
                JSR     TSTART
@wait:          LDA     SD_STATUS
                BIT     #SD_DATA_READY
                BNE     @put
                BIT     #SD_BUSY
                BEQ     @ended
                JSR     TICK
                BRA     @wait
@ended:         SEC
                RTS
@put:           LDA     (ptr),Y
                STA     SD_DATA
                INY
                BNE     @put
                INC     ptr+1
@put2:          LDA     (ptr),Y
                STA     SD_DATA
                INY
                BNE     @put2
                JMP     DONE

; buf into DATA, once there is room. Carry set if the command ended first.
PUTBUF:         LDA     #<buf
                LDX     #>buf
                STA     ptr
                STX     ptr+1
                JSR     TSTART
@wait:          LDA     SD_STATUS
                BIT     #SD_DATA_READY
                BNE     @put
                BIT     #SD_BUSY
                BEQ     @ended
                JSR     TICK
                BRA     @wait
@ended:         SEC
                RTS
@put:           LDY     #0
@lo:            LDA     buf,Y
                STA     SD_DATA
                INY
                BNE     @lo
@hi:            LDA     buf + 256,Y
                STA     SD_DATA
                INY
                BNE     @hi
                CLC
                RTS

; A sector out of DATA into buf. Carry set if the command ended instead.
GETBUF:         JSR     TSTART
@wait:          LDA     SD_STATUS
                BIT     #SD_DATA_READY
                BNE     @get
                BIT     #SD_BUSY
                BEQ     @ended
                JSR     TICK
                BRA     @wait
@ended:         SEC
                RTS
@get:           LDY     #0
@lo:            LDA     SD_DATA
                STA     buf,Y
                INY
                BNE     @lo
@hi:            LDA     SD_DATA
                STA     buf + 256,Y
                INY
                BNE     @hi
                CLC
                RTS

; Sector A/X of unit 1 into buf. Carry set if the card failed it.
READ1:          PHA
                JSR     IDLE
                STZ     SD_CTRL
                LDA     #1
                STA     SD_UNIT
                PLA
                STA     SD_LBA
                STX     SD_LBA + 1
                STZ     SD_LBA + 2
                STZ     SD_LBA + 3
                LDA     #1
                STA     SD_COUNT
                STZ     SD_COUNT + 1
                LDA     #SD_READ
                STA     SD_CMD
                JSR     GETBUF
                BCC     DONE
                RTS

; Wait for the command's end. Carry set if it failed. Keeps X and Y.
DONE:           JSR     TSTART
@wait:          LDA     SD_STATUS
                BIT     #SD_BUSY
                BEQ     @ended
                JSR     TICK
                BRA     @wait
@ended:         BIT     #SD_ERR
                BNE     @err
                CLC
                RTS
@err:           SEC
                RTS

; Wait for whatever the card is doing to end, before a command. Keeps X and Y.
IDLE:           JSR     TSTART
@wait:          LDA     SD_STATUS
                BIT     #SD_BUSY
                BEQ     @idle
                JSR     TICK
                BRA     @wait
@idle:          RTS

; A wait's count: 4 x 65,536 passes of TICK, about 30 cycles each: a second.
TSTART:         STZ     tmo
                STZ     tmo+1
                LDA     #4
                STA     tmo+2
                RTS
; One pass of a wait; once they're all gone, the command is abandoned and
; it's SD TIMEOUT. Keeps A, X and Y.
TICK:           DEC     tmo
                BNE     @more
                DEC     tmo+1
                BNE     @more
                DEC     tmo+2
                BEQ     SDTIMEOUT
@more:          RTS
SDTIMEOUT:      LDA     #SD_CTRL_RESET ; units stay mounted
                STA     SD_CTRL
                STZ     SD_CTRL
                LDA     #<MTIMEOUT
                LDX     #>MTIMEOUT
                JMP     FAIL

CLEARBUF:       LDA     #0
                TAY
@clear:         STA     buf,Y
                STA     buf + 256,Y
                INY
                BNE     @clear
                RTS

; ---- saying what, and back to the monitor ------------------------------------

; "DRIVES.TXT LINE n: " and the message at A/X.
LINE_FAIL:      PHA
                PHX
                JSR     LINE_SAY
                PLX
                PLA
                JMP     FAIL

; "DRIVES.TXT LINE n: ", n the line being read...
LINE_SAY:       LDA     line
                STA     num
                LDA     line+1
                STA     num+1
LINE_NUM:       LDA     #<MLINE
                LDX     #>MLINE
                JSR     SAY
                JSR     SAY_NUM
                LDA     #<MCOLON
                LDX     #>MCOLON
                JMP     SAY

; ...or drive seg's line, while mounting.
DRIVE_SAY:      LDA     seg
                ASL
                TAX
                LDA     lines,X
                STA     num
                LDA     lines+1,X
                STA     num+1
                BRA     LINE_NUM

; Drive seg's path, from its slot.
SAY_PATH:       LDA     seg
                ASL
                CLC
                ADC     #>(slots + 2)
                TAX
                LDA     #<(slots + 2)
                JMP     SAY

; "OODOS.SYS: " and "SEGMENT n " and the message at A/X.
SEG_FAIL:       PHA
                PHX
                JSR     SYS_SAY
                LDA     #<MSEGMENT
                LDX     #>MSEGMENT
                JSR     SAY
                LDA     seg
                INC
                JSR     SAY_BYTE
                PLX
                PLA
                BRA     FAIL

SYS_SAY:        LDA     #<MSYS
                LDX     #>MSYS
                JMP     SAY

; The CP/M reader's error A, on drive seg.
CRFAIL:         CMP     #CRE_SD
                BEQ     SDFAIL_CR
                CMP     #CRE_TIMEOUT
                BNE     @known
                JMP     SDTIMEOUT
@known:         CMP     #CRE_BIG
                BEQ     @big
                CMP     #CRE_UNIT
                BEQ     @unit
                CMP     #CRE_SIZE
                BEQ     @size
                LDA     #<MDAMAGED
                LDX     #>MDAMAGED
                BRA     FAIL
@big:           LDA     #<MBIG
                LDX     #>MBIG
                BRA     FAIL
@unit:          LDA     #<MUNMOUNTED   ; mounted, then gone from the units table
                LDX     #>MUNMOUNTED
                BRA     @drive
@size:          LDA     #<MSIZE
                LDX     #>MSIZE
@drive:         PHA                    ; DRIVES.TXT LINE n: PATH and why
                PHX
                JSR     DRIVE_SAY
                JSR     SAY_PATH
                PLX
                PLA
                BRA     FAIL

SDFAIL_CR:      LDA     CR_SDERR
                BRA     SDFAIL_A
SDFAIL:         LDA     SD_ERROR
SDFAIL_A:       PHA
                LDA     #<MSDERROR
                LDX     #>MSDERROR
                JSR     SAY
                PLA
                JSR     HEX
                LDA     #<MEND
                LDX     #>MEND
                ; fall through

; Say the message at A/X, then the windows back as C80CR left them and back
; to it, from however deep: it goes on to the monitor.
FAIL:           JSR     SAY
                LDA     #1
                STA     NB_WIN1
                LDA     #2
                STA     NB_WIN2
                LDX     stack
                TXS
                RTS

; The message at A/X, to its NUL.
SAY:            STA     ptr
                STX     ptr+1
                LDY     #0
@next:          LDA     (ptr),Y
                BEQ     @done
                JSR     ECHO
                INY
                BNE     @next
                INC     ptr+1
                BRA     @next
@done:          RTS

; A in decimal.
SAY_BYTE:       STA     num
                STZ     num+1
; num in decimal, without leading zeros.
SAY_NUM:        LDX     #0             ; the power of ten
                STZ     ptr            ; a digit said yet
@power:         LDY     #'0'
@sub:           LDA     num
                CMP     TENS_LO,X
                LDA     num+1
                SBC     TENS_HI,X
                BCC     @digit
                STA     num+1
                LDA     num
                SBC     TENS_LO,X
                STA     num
                INY
                BRA     @sub
@digit:         TYA
                CPX     #4
                BEQ     @say           ; the units, always
                CMP     #'0'
                BNE     @say
                LDA     ptr
                BEQ     @skip
                TYA
@say:           JSR     ECHO
                INC     ptr
@skip:          INX
                CPX     #5
                BNE     @power
                RTS

; A in hex.
HEX:            PHA
                LSR
                LSR
                LSR
                LSR
                JSR     @digit
                PLA
                AND     #$0F
@digit:         ORA     #'0'
                CMP     #'9' + 1
                BCC     @say
                ADC     #6
@say:           JMP     ECHO

; Carry set if segment X's start >= the address in A (low), num (middle),
; Y (high).
START_GE:       STY     num+1
                STA     ptr
                LDA     starts,X
                CMP     ptr
                LDA     starts + SYS_MAX_SEGMENTS,X
                SBC     num
                LDA     starts + 2 * SYS_MAX_SEGMENTS,X
                SBC     num+1
                RTS

; Carry set if segment X's end <= the address in A (low), num (middle), Y (high).
END_LE:         STY     num+1
                CMP     ends,X
                LDA     num
                SBC     ends + SYS_MAX_SEGMENTS,X
                LDA     num+1
                SBC     ends + 2 * SYS_MAX_SEGMENTS,X
                RTS

; Carry set if segments X and Y overlap: X's start < Y's end and Y's start < X's end.
OVERLAP:        LDA     starts,X
                CMP     ends,Y
                LDA     starts + SYS_MAX_SEGMENTS,X
                SBC     ends + SYS_MAX_SEGMENTS,Y
                LDA     starts + 2 * SYS_MAX_SEGMENTS,X
                SBC     ends + 2 * SYS_MAX_SEGMENTS,Y
                BCS     @no
                LDA     starts,Y
                CMP     ends,X
                LDA     starts + SYS_MAX_SEGMENTS,Y
                SBC     ends + SYS_MAX_SEGMENTS,X
                LDA     starts + 2 * SYS_MAX_SEGMENTS,Y
                SBC     ends + 2 * SYS_MAX_SEGMENTS,X
                BCS     @no
                SEC
                RTS
@no:            CLC
                RTS

.segment "RODATA"

TENS_LO:        .byte   <10000, <1000, <100, <10, <1
TENS_HI:        .byte   >10000, >1000, >100, >10, >1
MAGIC:          .byte   SYS_MAGIC_0, SYS_MAGIC_1, SYS_MAGIC_2, SYS_MAGIC_3
BOM:            .byte   $EF, $BB, $BF
SYS_NAME:       .byte   "OODOS   SYS"
OODOS_DIR:      .byte   "/OODOS"
OODOS_DIR_LEN   = * - OODOS_DIR
DRIVES_PATH:    .byte   "/OODOS/"
DRIVES_NAME:    .byte   "DRIVES.TXT", 0

MBANNER:        .byte   $0D, "OOLDR ", '0' + SYS_MAJOR, '.', '0' + SYS_MINOR, $0D, 0
MNODRIVES:      .byte   $0D, "NO /OODOS/DRIVES.TXT ON THE CARD", $0D, 0
MLINE:          .byte   $0D, "DRIVES.TXT LINE ", 0
MCOLON:         .byte   ": ", 0
MNOTDRIVE:      .byte   "NOT A DRIVE A-G AND A COLON", $0D, 0
MNOBLANK:       .byte   "NO SPACE OR TAB AFTER THE COLON", $0D, 0
MNOSLASH:       .byte   "THE PATH DOESN'T START WITH /", $0D, 0
MCONTROL:       .byte   "A CONTROL CHARACTER IN THE PATH", $0D, 0
MLONGPATH:      .byte   "THE PATH IS OVER 509 BYTES", $0D, 0
MWORD:          .byte   "ONLY RO AND A COMMENT MAY FOLLOW THE PATH", $0D, 0
MCR:            .byte   "A CR NOT BEFORE LF", $0D, 0
MTWICE:         .byte   ": NAMED TWICE", $0D, 0
MSAME:          .byte   " IS DRIVE ", 0
MSAME2:         .byte   ": ALREADY", $0D, 0
MNOA:           .byte   $0D, "DRIVES.TXT NAMES NO A:", $0D, 0
MNOTFOUND:      .byte   " IS NOT ON THE CARD", $0D, 0
MSIZE:          .byte   " IS NOT 8 MB OR 32 MB", $0D, 0
MUNMOUNTED:     .byte   " IS NOT MOUNTED", $0D, 0
MNAME:          .byte   "AN EMPTY, . OR .. NAME IN THE PATH", $0D, 0
MTOOBIG:        .byte   $0D, "DRIVES.TXT IS TOO BIG", $0D, 0
MTIMEOUT:       .byte   $0D, "SD TIMEOUT", $0D, 0
MNOSYS:         .byte   $0D, "NO OODOS.SYS ON A:", $0D, 0
MSYS:           .byte   $0D, "OODOS.SYS: ", 0
MEMPTY:         .byte   $0D, "OODOS.SYS: EMPTY", $0D, 0
MMAGIC:         .byte   "NOT OODOS.SYS (NO OOSY)", $0D, 0
MVERSION:       .byte   "MAJOR VERSION ", 0
MVERSION2:      .byte   ", NOT ", '0' + SYS_MAJOR, $0D, 0
MCOUNT:         .byte   " SEGMENTS, NOT 1-8", $0D, 0
MENTRY:         .byte   "ENTRY NOT IN $C800-$FFFF", $0D, 0
MSEGMENT:       .byte   "SEGMENT ", 0
MLONG:          .byte   " IS OVER 16 KB", $0D, 0
MEMPTYSEG:      .byte   " IS EMPTY", $0D, 0
MRECORD0:       .byte   " STARTS IN THE HEADER", $0D, 0
MNOENTRY:       .byte   "THE ENTRY IS IN NO SEGMENT", $0D, 0
MOUTSIDE:       .byte   " IS NOT IN BLOCK 31 OR BLOCKS 60-62", $0D, 0
MPAST:          .byte   " RUNS PAST THE FILE'S END", $0D, 0
MSEGMENTS:      .byte   "SEGMENTS ", 0
MAND:           .byte   " AND ", 0
MOVERLAP:       .byte   " OVERLAP", $0D, 0
MDAMAGED:       .byte   $0D, "A: A DAMAGED DIRECTORY ENTRY FOR OODOS.SYS", $0D, 0
MBIG:           .byte   $0D, "A: OODOS.SYS HAS TOO MANY DIRECTORY ENTRIES", $0D, 0
MSDERROR:       .byte   $0D, "SD ERROR ", 0
MEND:           .byte   $0D, 0

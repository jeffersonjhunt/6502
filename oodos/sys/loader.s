; OODOS's loader (hw/oodos.md, "The RSXs, phase 5"), in block 62's code
; window with the CCP: function 59 for programs, and the CCP's own loads,
; through the same door. It runs in its caller's context (the program's
; windows and stack, W3 on block 62: the stub's door puts it there) and
; calls the BDOS at $BFFD, the chain's end, so no RSX sees its reads. It
; uses no zero page: its pointers are in its own instructions.
;
;   LOADER   59: A/X an opened FCB, the load address in its R0-R1; A/X = 0
;            only takes out the RSXs whose remove flag is set
;   LINK     $0203, $0204 and each RSX's offsets 9 and 12 from the table
;   REMOVE   the RSXs whose remove flag is $FF out of the table (but those
;            KEEPNEW marked), and any CHECKRSX would drop; then LINK
;   CHECKRSX the RSXs whose offsets 6 and 9 aren't JMPs out (a program
;            wrote over them), then LINK
;   KEEPNEW  the last load's RSXs marked kept (an RSX-only .COM's, for the
;            next program); UNKEEP none kept
;   TOPADDR  the TPA's top + 1: the lowest RSX's first byte, or $BFFD
;   APPLYSCB the SCB entries the last load found (GENCOM's [SCB=])
;   CCPSET   CCPST to the SCB's $19
;
; 59's returns (the Programmer's Guide's): A = 0 done; A = $FE refused,
; with X (ours) 0 an address or no room, 1 a header, 2 zero page, 3 not a
; program (no magic number, an empty file, or nothing in it to load); else
; function 20's: A = 1 the file ends inside what it says it holds, A = $FF
; and X the physical error. A refusal changes nothing past the image, and
; RSXs it had linked are taken out again. The caller's DMA address and
; segment, error mode and multi-sector count are put back, and R0-R2 hold
; the load address again.
;
; Called by the CCP (CCPST bit 7), 59 loads only a .COM (flags 0), with its
; attached RSXs, and the SCB entries wait for APPLYSCB as the program
; starts; called by a program, it loads a .COM at $0300 or a .PRL overlay at
; a page from $0300, refuses a file with RSXs attached (X = 4: only the
; CCP's loads attach them), applies the SCB entries itself, and takes the
; flagged RSXs out first (but those KEEPNEW kept: an RSX-only .COM's RSXs
; stay for the next program).

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.export LOADER, LINK, REMOVE, CHECKRSX, KEEPNEW, UNKEEP, TOPADDR, APPLYSCB, CCPSET
.export NRSX, KEEP, WBF, CCPST, LHDR, TBASE, TZP, TZN

FCB_R0          = 33                   ; the FCB's random record, R0-R2
RSX_MAX         = 15                   ; as many as one GENCOM header holds (ours)
RECORD          = 128
SCB_CCPST       = $19                  ; the SCB's byte for redirecting RSXs (ours)

.segment "RESBSS"
TBASE:          .res    RSX_MAX        ; each RSX's first page, 0 the highest (oldest)
TZP:            .res    RSX_MAX        ; its zero page slice, from $F0
TZN:            .res    RSX_MAX
TKEEP:          .res    RSX_MAX        ; 1: kept from an RSX-only .COM for the next program
RMODE:          .res    1              ; REMOVE's: 0 only the overwritten, else the flagged too
LBUF:           .res    RECORD         ; the loader's record
LHDR:           .res    OH_LEN         ; the file's header, for the CCP too
RHDR:           .res    OH_LEN         ; an RSX's
ENT:            .res    RT_LEN         ; an attached RSX table entry
LSAVE:          .res    4              ; the caller's DMA address and segment (52)
LERR:           .res    1              ; its error mode and multi-sector count
LMULT:          .res    1
LFCB:           .res    2              ; the FCB
LADDR:          .res    2              ; the load address
LREC:           .res    3              ; the next record to read
LDEST:          .res    2              ; LIMAGE's
LLEFT:          .res    2
LCOUNT:         .res    1
LN0:            .res    1              ; NRSX as the load began
LEI:            .res    1              ; the table entry
SCBN:           .res    1              ; SCB entries found
SCBP:           .res    2 * RSX_MAX    ;   their offsets and values
RBASE:          .res    1              ; an RSX's page, zero page, size of it
RZ:             .res    1
RZN:            .res    1
RTOT:           .res    2              ; its image and BSS
RD:             .res    1              ; RELOC's: the delta, the bits, the bytes left, where
RBITS:          .res    1
RLEFT:          .res    2
RPTR:           .res    2
TLO:            .res    1              ; LINK's target
THI:            .res    1
TMP:            .res    2

.segment "RES"

; In the file, so 0 from the cold boot.
NRSX:           .byte   0              ; RSXs in the table
KEEP:           .byte   0              ; 1 an RSX-only .COM's RSXs kept (TKEEP), 2 their program running
WBF:            .byte   0              ; 1 while the warm boot's function 0 goes down the chain
CCPST:          .byte   0              ; bit 7 the CCP runs, bits 0-6 its command count
SCBPB:          .byte   0, 0, 0, 0     ; 49's block

; ---------------------------------------------------------------------------
; 59.

LOADER:         STA     LFCB
                STX     LFCB + 1
                STA     FCBGET + 1
                STA     FCBPUT + 1
                STX     FCBGET + 2
                STX     FCBPUT + 2
                LDA     #<LSAVE        ; the caller's state, kept
                LDX     #>LSAVE
                LDY     #F_DMAGET
                JSR     BDOS_ENTRY
                LDX     #<(SCB_ERRMODE - SCB)
                JSR     SCBGET
                STA     LERR
                LDX     #<(SCB_MULTI - SCB)
                JSR     SCBGET
                STA     LMULT
                LDA     #$FF           ; the loader's: no DMA segment, errors
                TAX                    ; returned, a record at a time
                LDY     #F_DMASEG
                JSR     BDOS_ENTRY
                LDA     #$FF
                LDY     #F_ERRMODE
                JSR     BDOS_ENTRY
                LDA     #1
                LDY     #F_MULTISEC
                JSR     BDOS_ENTRY
                LDA     NRSX
                STA     LN0
                STZ     SCBN
                BIT     CCPST          ; a program's 59: the flagged RSXs out first
                BMI     @kept          ; (the Guide's 4.4.1), but an RSX-only .COM's
                JSR     REMOVE
                LDA     NRSX
                STA     LN0
@kept:          LDA     LFCB
                ORA     LFCB + 1
                BNE     @file
                JMP     DONE           ; 0: only that (ours)
@file:          LDY     #FCB_R0        ; the load address
                JSR     FCBGET
                STA     LADDR
                INY
                JSR     FCBGET
                STA     LADDR + 1
                STZ     LREC           ; record 0: the header
                STZ     LREC + 1
                STZ     LREC + 2
                LDA     #<LBUF
                LDX     #>LBUF
                JSR     RREAD
                BCC     @hdr
                CMP     #$FF
                BEQ     @rderr
                JMP     FAIL3          ; an empty file
@rderr:         JMP     FAILRD
@hdr:           LDX     #OH_LEN - 1
@copy:          LDA     LBUF,X
                STA     LHDR,X
                DEX
                BPL     @copy
                JSR     HDROK          ; the magic number, flags, page, address
                BCC     @rsxs
                JMP     FAILX
@rsxs:          LDA     LHDR + OH_RSXCOUNT
                BEQ     @image
                BIT     CCPST          ; attached RSXs: the CCP's loads only
                BMI     @ccps
                LDA     #$FE
                LDX     #4
                JMP     FAILX
@ccps:          STZ     LEI
@entry:         JSR     ENTRY          ; each entry of the attached RSX table
                BCC     @next
                JMP     FAILX
@next:          INC     LEI
                LDA     LEI
                CMP     LHDR + OH_RSXCOUNT
                BNE     @entry
                JSR     LINK
@image:         LDA     LHDR + OH_SIZE
                ORA     LHDR + OH_SIZE + 1
                BNE     @fit
                LDA     LHDR + OH_RSXCOUNT ; size 0: RSXs only, or nothing
                BEQ     @empty
                JMP     DONE
@empty:         JMP     FAIL3
@fit:           CLC                    ; the load address + size + BSS <= the top
                LDA     LADDR
                ADC     LHDR + OH_SIZE
                STA     TMP
                LDA     LADDR + 1
                ADC     LHDR + OH_SIZE + 1
                STA     TMP + 1
                BCS     @big
                CLC
                LDA     TMP
                ADC     LHDR + OH_BSS
                STA     TMP
                LDA     TMP + 1
                ADC     LHDR + OH_BSS + 1
                STA     TMP + 1
                BCS     @big
                JSR     TOPADDR
                STA     RPTR           ; (for a moment)
                SEC
                LDA     RPTR
                SBC     TMP
                TXA
                SBC     TMP + 1
                BCS     @load
@big:           JMP     FAIL0
@load:          STZ     LREC           ; the image, from record 0
                STZ     LREC + 1
                LDA     LADDR
                STA     LDEST
                LDA     LADDR + 1
                STA     LDEST + 1
                LDA     LHDR + OH_SIZE
                STA     LLEFT
                LDA     LHDR + OH_SIZE + 1
                STA     LLEFT + 1
                JSR     LIMAGE
                BCS     FAILR
                LDA     LHDR + OH_FLAGS ; a .PRL: its pages moved (no zero page: an overlay)
                BEQ     DONE
                SEC
                LDA     LADDR + 1
                SBC     LHDR + OH_PAGE
                STA     RD
                LDX     #0             ; the page bitmap's record, from 0
                JSR     BITREC
                LDA     LADDR
                STA     RPTR
                LDA     LADDR + 1
                STA     RPTR + 1
                LDA     LHDR + OH_SIZE
                STA     RLEFT
                LDA     LHDR + OH_SIZE + 1
                STA     RLEFT + 1
                JSR     RELOC
                BCS     FAILR
                ; falls through
DONE:           JSR     PUTBACK
                BIT     CCPST          ; a program's: the SCB entries now
                BMI     @ccp
                JSR     APPLYSCB
@ccp:           LDA     #0
                TAX
                RTS

; The refusals: A = $FE, X why; A from a read.
FAIL0:          LDX     #0
                BRA     FAILFE
FAIL3:          LDX     #3
FAILFE:         LDA     #$FE
FAILX:          CMP     #$FF           ; a read's code: 1 for any but a physical error
                BEQ     FAIL
                CMP     #$FE
                BEQ     FAIL
                LDA     #1
                BRA     FAIL
FAILR:          CMP     #$FF
                BNE     FAILX
FAILRD:         LDA     #$FF
FAIL:           STA     TMP
                STX     TMP + 1
                LDA     NRSX           ; the RSXs it linked out again
                CMP     LN0
                BEQ     @same
                LDA     LN0
                STA     NRSX
                JSR     LINK
@same:          JSR     PUTBACK
                STZ     SCBN
                LDA     TMP
                LDX     TMP + 1
                RTS

; The caller's state back, and R0-R2 the load address.
PUTBACK:        LDA     LSAVE
                LDX     LSAVE + 1
                LDY     #F_DMAOFF
                JSR     BDOS_ENTRY
                LDA     LSAVE + 2
                LDX     LSAVE + 3
                LDY     #F_DMASEG
                JSR     BDOS_ENTRY
                LDA     LERR
                LDX     #<(SCB_ERRMODE - SCB)
                JSR     SCBSET
                LDA     LMULT
                LDX     #<(SCB_MULTI - SCB)
                JSR     SCBSET
                LDA     LFCB
                ORA     LFCB + 1
                BEQ     @none
                LDY     #FCB_R0
                LDA     LADDR
                JSR     FCBPUT
                INY
                LDA     LADDR + 1
                JSR     FCBPUT
                INY
                LDA     #0
                JSR     FCBPUT
@none:          RTS

; LHDR's file may load where it is asked: carry clear, or set with A = $FE
; and X why.
HDROK:          LDX     #3
@magic:         LDA     LHDR,X
                CMP     MAGIC,X
                BNE     @none
                DEX
                BPL     @magic
                LDA     LHDR + OH_FLAGS
                BEQ     @com
                BIT     CCPST          ; the CCP runs .COMs only
                BMI     @bad
                CMP     #OF_RELOC      ; a .PRL overlay: at a page from $0300, no zero page
                BNE     @bad
                LDA     LADDR
                BNE     @addr
                LDA     LADDR + 1
                CMP     #>BP_TPA
                BCC     @addr
                LDA     LHDR + OH_ZPSIZE
                BNE     @zp
                CLC
                RTS
@com:           LDA     LHDR + OH_PAGE ; a .COM: linked at $0300 and loaded there
                CMP     #>BP_TPA
                BNE     @bad
                LDA     LADDR
                BNE     @addr
                LDA     LADDR + 1
                CMP     #>BP_TPA
                BNE     @addr
                CLC
                RTS
@none:          LDX     #3
                BRA     @no
@addr:          LDX     #0
                BRA     @no
@zp:            LDX     #2
                BRA     @no
@bad:           LDX     #1
@no:            LDA     #$FE
                SEC
                RTS

; Entry LEI of the attached RSX table: an SCB entry kept, a non-banked one
; passed over, an RSX loaded. Carry set with A and X if it fails.
ENTRY:          LDA     LEI            ; its record: the table's + LEI / 8
                LSR     A
                LSR     A
                LSR     A
                CLC
                ADC     LHDR + OH_RSXREC
                STA     LREC
                LDA     LHDR + OH_RSXREC + 1
                ADC     #0
                STA     LREC + 1
                STZ     LREC + 2
                LDA     #<LBUF
                LDX     #>LBUF
                JSR     RREAD
                BCS     @rd
                LDA     LEI            ; at (LEI mod 8) x 16
                AND     #7
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                TAX
                LDY     #0
@copy:          LDA     LBUF,X
                STA     ENT,Y
                INX
                INY
                CPY     #RT_LEN
                BNE     @copy
                LDA     ENT + RT_FLAGS
                AND     #RT_SCB
                BEQ     @rsx
                LDX     SCBN           ; an SCB byte, for later
                CPX     #RSX_MAX
                BCS     @bad
                TXA
                ASL     A
                TAX
                LDA     ENT + RT_SCBOFF
                STA     SCBP,X
                LDA     ENT + RT_SCBVAL
                STA     SCBP + 1,X
                INC     SCBN
                CLC
                RTS
@rsx:           LDA     ENT + RT_FLAGS ; non-banked only: passed over (OODOS is banked)
                AND     #RT_NONBANK
                BEQ     LOADRSX
                CLC
                RTS
@bad:           LDA     #$FE
                LDX     #1
                SEC
@rd:            RTS

; The RSX at ENT's record: below the lowest, its zero page given, relocated,
; linked into the table (LINK is the caller's). Carry set with A and X if
; it fails; carry clear and nothing added if it is a non-banked RSX.
LOADRSX:        LDA     NRSX
                CMP     #RSX_MAX
                BCC     @go
                JMP     RSXROOM
@go:
                LDA     ENT + RT_REC   ; its header
                STA     LREC
                LDA     ENT + RT_REC + 1
                STA     LREC + 1
                STZ     LREC + 2
                LDA     #<LBUF
                LDX     #>LBUF
                JSR     RREAD
                BCC     @hdr
                RTS
@hdr:           LDX     #OH_LEN - 1
@copy:          LDA     LBUF,X
                STA     RHDR,X
                DEX
                BPL     @copy
                LDX     #3
@magic:         LDA     RHDR,X
                CMP     MAGIC,X
                BNE     @bad1
                DEX
                BPL     @magic
                LDA     RHDR + OH_FLAGS
                CMP     #OF_RELOC | OF_RSX
                BEQ     @flags
@bad1:          JMP     RSXBAD
@flags:
                LDA     RHDR + OH_SIZE + 1 ; the prefix's 27 bytes at least
                BNE     @long
                LDA     RHDR + OH_SIZE
                CMP     #RSX_PREFIX_LEN
                BCC     @bad
@long:          LDA     RHDR + OH_ZPSIZE
                STA     RZN
                JSR     ZALLOC
                BCS     @zp
                CLC                    ; its image and BSS, below the lowest
                LDA     RHDR + OH_SIZE
                ADC     RHDR + OH_BSS
                STA     RTOT
                LDA     RHDR + OH_SIZE + 1
                ADC     RHDR + OH_BSS + 1
                STA     RTOT + 1
                BCS     @room
                JSR     TOPADDR        ; the page: (top - total) rounded down
                STX     TMP + 1
                SEC
                SBC     RTOT
                TXA
                SBC     RTOT + 1
                BCC     @room
                CMP     #>BP_TPA
                BCC     @room
                STA     RBASE
                STA     LDEST + 1
                STZ     LDEST
                LDA     ENT + RT_REC
                STA     LREC
                LDA     ENT + RT_REC + 1
                STA     LREC + 1
                STZ     LREC + 2
                LDA     RHDR + OH_SIZE
                STA     LLEFT
                LDA     RHDR + OH_SIZE + 1
                STA     LLEFT + 1
                JSR     LIMAGE
                BCC     @prefix
                RTS
@room:          JMP     RSXROOM
@zp:            LDA     #$FE
                LDX     #2
                SEC
                RTS
@bad:           JMP     RSXBAD
@prefix:        LDA     RBASE          ; offsets 6 and 9 JMPs
                JSR     SETPG
                LDY     #RSX_START
                JSR     PEEKY
                CMP     #$4C
                BNE     @bad
                LDY     #RSX_NEXT
                JSR     PEEKY
                CMP     #$4C
                BNE     @bad
                LDY     #RSX_NONBANK   ; non-banked only: passed over
                JSR     PEEKY
                CMP     #$FF
                BNE     @reloc
                CLC
                RTS
@reloc:         SEC                    ; its pages moved
                LDA     RBASE
                SBC     RHDR + OH_PAGE
                STA     RD
                LDX     #0
                JSR     RBITREC
                JSR     RPREP
                JSR     RELOC
                BCC     @zpm
                RTS
@zpm:           LDA     RZN            ; its zero page moved
                BEQ     @serial
                SEC
                LDA     RZ
                SBC     RHDR + OH_ZPBASE
                STA     RD
                LDX     #1
                JSR     RBITREC
                JSR     RPREP
                JSR     RELOC
                BCC     @serial
                RTS
@serial:        LDA     #0             ; 107's serial number at offset 0
                LDX     RBASE
                LDY     #S_SERIAL
                JSR     BDOS_ENTRY
                LDA     RBASE
                JSR     SETPG
                LDY     #RSX_LOADER    ; the loader flag 0, as the Guide's LOADER
                LDA     #0
                JSR     POKEY
                LDX     NRSX
                LDA     RBASE
                STA     TBASE,X
                LDA     RZ
                STA     TZP,X
                LDA     RZN
                STA     TZN,X
                STZ     TKEEP,X
                INC     NRSX
                CLC
                RTS

RSXROOM:        LDA     #$FE
                LDX     #0
                SEC
                RTS
RSXBAD:         LDA     #$FE
                LDX     #1
                SEC
                RTS

; RELOC's image: the RSX's, at RBASE, RHDR's size.
RPREP:          STZ     RPTR
                LDA     RBASE
                STA     RPTR + 1
                LDA     RHDR + OH_SIZE
                STA     RLEFT
                LDA     RHDR + OH_SIZE + 1
                STA     RLEFT + 1
                RTS

; LREC: the record of bitmap X (0 pages, 1 zero page) of RHDR's file, which
; starts at ENT's record; BITREC the same for LHDR's, from record 0.
RBITREC:        PHX
                LDA     RHDR + OH_SIZE
                LDY     RHDR + OH_SIZE + 1
                PLX
                JSR     BITOFF
                CLC
                LDA     LREC
                ADC     ENT + RT_REC
                STA     LREC
                LDA     LREC + 1
                ADC     ENT + RT_REC + 1
                STA     LREC + 1
                RTS
BITREC:         LDA     LHDR + OH_SIZE
                LDY     LHDR + OH_SIZE + 1
                ; falls through
; LREC = the bitmap's record from the file's start, for an image of A/Y
; bytes: ceil((16 + size) / 128), and for X = 1 ceil(((size + 7) / 8) / 128)
; more.
BITOFF:         STA     TMP
                STY     TMP + 1
                PHX
                CLC                    ; 16 + size, 17 bits
                LDA     TMP
                ADC     #OH_LEN
                STA     LREC
                LDA     TMP + 1
                ADC     #0
                STA     LREC + 1
                LDA     #0
                ROL     A
                STA     LREC + 2
                JSR     RECS
                PLX
                BEQ     @done
                LDA     LREC           ; the page bitmap's record, kept
                PHA
                LDA     LREC + 1
                PHA
                CLC                    ; (size + 7) / 8
                LDA     TMP
                ADC     #7
                STA     LREC
                LDA     TMP + 1
                ADC     #0
                STA     LREC + 1
                LDA     #0
                ROL     A
                STA     LREC + 2
                LDX     #3
@shift:         LSR     LREC + 2
                ROR     LREC + 1
                ROR     LREC
                DEX
                BNE     @shift
                JSR     RECS
                PLA
                STA     TMP + 1
                PLA
                CLC
                ADC     LREC
                STA     LREC
                LDA     TMP + 1
                ADC     LREC + 1
                STA     LREC + 1
@done:          RTS

; LREC (24 bits, below $20000) = ceil(LREC / 128).
RECS:           CLC
                LDA     LREC
                ADC     #RECORD - 1
                STA     LREC
                LDA     LREC + 1
                ADC     #0
                STA     LREC + 1
                LDA     LREC + 2
                ADC     #0
                STA     LREC + 2
                ASL     LREC           ; x 2, then the top two bytes are / 128
                ROL     LREC + 1
                ROL     LREC + 2
                LDA     LREC + 1
                STA     LREC
                LDA     LREC + 2
                STA     LREC + 1
                STZ     LREC + 2
                RTS

; RLEFT bytes from RPTR, each its bitmap marks (from LREC, a record at a
; time) increased by RD. Carry set with A and X on a read's failure.
RELOC:          LDA     RLEFT
                ORA     RLEFT + 1
                BNE     @rec
                CLC
                RTS
@rec:           LDA     #<LBUF
                LDX     #>LBUF
                JSR     RREAD
                BCC     @got
                RTS
@got:           LDY     #0
@byte:          LDA     LBUF,Y
                STA     RBITS
                LDA     RPTR           ; the eight bytes it covers
                STA     @ld + 1
                STA     @st + 1
                LDA     RPTR + 1
                STA     @ld + 2
                STA     @st + 2
                LDX     #0
@bit:           LDA     RLEFT
                ORA     RLEFT + 1
                BEQ     @end
                ASL     RBITS
                BCC     @next
@ld:            LDA     $FFFF,X
                CLC
                ADC     RD
@st:            STA     $FFFF,X
@next:          LDA     RLEFT
                BNE     @dec
                DEC     RLEFT + 1
@dec:           DEC     RLEFT
                INX
                CPX     #8
                BNE     @bit
                CLC
                LDA     RPTR
                ADC     #8
                STA     RPTR
                BCC     @on
                INC     RPTR + 1
@on:            INY
                BPL     @byte
                BRA     RELOC
@end:           CLC
                RTS

; LLEFT bytes of image to LDEST, from LREC's record (the file's header
; record: the image starts at its byte 16): whole records straight there,
; the first and a last part through LBUF. Carry set with A and X on a
; read's failure.
LIMAGE:         LDA     #<LBUF
                LDX     #>LBUF
                JSR     RREAD
                BCC     @first
                RTS
@first:         LDA     #OH_LEN
                JSR     COPYBUF
@whole:         LDA     LLEFT + 1
                BNE     @straight
                LDA     LLEFT
                BEQ     @done
                BPL     @part
@straight:      LDA     LDEST
                LDX     LDEST + 1
                JSR     RREAD
                BCC     @on
                RTS
@on:            CLC
                LDA     LDEST
                ADC     #RECORD
                STA     LDEST
                BCC     @less
                INC     LDEST + 1
@less:          SEC
                LDA     LLEFT
                SBC     #RECORD
                STA     LLEFT
                BCS     @whole
                DEC     LLEFT + 1
                BRA     @whole
@part:          LDA     #<LBUF
                LDX     #>LBUF
                JSR     RREAD
                BCC     @last
                RTS
@last:          LDA     #0
                JSR     COPYBUF
@done:          CLC
                RTS

; LBUF from offset A to its end, or LLEFT bytes if fewer, to LDEST; both
; move on. Nothing when LLEFT is 0.
COPYBUF:        LDX     LLEFT
                BNE     @some
                LDX     LLEFT + 1
                BNE     @some
                RTS
@some:          CLC
                ADC     #<LBUF
                STA     @from + 1
                LDA     #>LBUF
                ADC     #0
                STA     @from + 2
                LDA     LDEST
                STA     @to + 1
                LDA     LDEST + 1
                STA     @to + 2
                SEC                    ; 128 - offset, or LLEFT if fewer
                LDA     #<(LBUF + RECORD)
                SBC     @from + 1
                LDX     LLEFT + 1
                BNE     @n
                CMP     LLEFT
                BCC     @n
                LDA     LLEFT
@n:             STA     LCOUNT
                LDY     #0
@byte:
@from:          LDA     $FFFF,Y
@to:            STA     $FFFF,Y
                INY
                CPY     LCOUNT
                BNE     @byte
                CLC
                LDA     LDEST
                ADC     LCOUNT
                STA     LDEST
                BCC     @less
                INC     LDEST + 1
@less:          SEC
                LDA     LLEFT
                SBC     LCOUNT
                STA     LLEFT
                BCS     @done
                DEC     LLEFT + 1
@done:          RTS

; Record LREC of the file to the DMA address A/X by 33, then LREC on by one.
; Carry set with A and X (the BDOS's) if it fails.
RREAD:          LDY     #F_DMAOFF
                JSR     BDOS_ENTRY
                LDY     #FCB_R0
                LDA     LREC
                JSR     FCBPUT
                INY
                LDA     LREC + 1
                JSR     FCBPUT
                INY
                LDA     LREC + 2
                JSR     FCBPUT
                LDA     LFCB
                LDX     LFCB + 1
                LDY     #F_READRAND
                JSR     BDOS_ENTRY
                CMP     #0
                BNE     @err
                INC     LREC
                BNE     @ok
                INC     LREC + 1
                BNE     @ok
                INC     LREC + 2
@ok:            CLC
                RTS
@err:           SEC
                RTS

; The FCB's byte Y (LOADER sets the operands).
FCBGET:         LDA     $FFFF,Y
                RTS
FCBPUT:         STA     $FFFF,Y
                RTS

; Byte Y of page A's RSX (SETPG sets the page).
SETPG:          STA     PEEKY + 2
                STA     POKEY + 2
                RTS
PEEKY:          LDA     $FF00,Y
                RTS
POKEY:          STA     $FF00,Y
                RTS

; RZ: the lowest run of RZN free bytes in $F0-$FF, carry clear; or carry set.
ZALLOC:         LDA     #$F0
                STA     RZ
                LDA     RZN
                BNE     @try
                CLC                    ; none: no slice
                RTS
@try:           CLC                    ; RZ + RZN past $100: none fits
                LDA     RZ
                ADC     RZN
                BCC     @in
                BNE     @full          ; (exactly $100 fits)
@in:            LDX     NRSX
@each:          DEX
                BMI     @free
                LDA     TZN,X
                BEQ     @each
                CLC                    ; overlaps if RZ < TZP + TZN and TZP < RZ + RZN
                LDA     TZP,X
                ADC     TZN,X
                BCS     @hi            ; (TZP + TZN = $100: past any RZ)
                CMP     RZ
                BCC     @each
                BEQ     @each
@hi:            CLC
                LDA     RZ
                ADC     RZN
                BCS     @over          ; RZ + RZN = $100: past any TZP
                CMP     TZP,X
                BCC     @each
                BEQ     @each
@over:          INC     RZ
                BNE     @try
@full:          SEC
                RTS
@free:          CLC
                RTS

; ---------------------------------------------------------------------------
; The chain.

; The TPA's top + 1, A/X: the lowest RSX's first byte, or $BFFD.
TOPADDR:        LDX     NRSX
                BEQ     @bdos
                LDA     TBASE - 1,X
                TAX
                LDA     #0
                RTS
@bdos:          LDA     #<BDOS_ENTRY
                LDX     #>BDOS_ENTRY
                RTS

; $0203 to the lowest RSX's offset 6, each RSX's offset 9 to the next above
; (the highest's to $BFFD), and each offset 12 the JMP that leads to it:
; $0203 for the lowest, else the offset 9 of the RSX below it (ours).
LINK:           LDA     #<BDOS_ENTRY
                STA     TLO
                LDA     #>BDOS_ENTRY
                STA     THI
                LDX     #0
@next:          CPX     NRSX
                BEQ     @base
                LDA     TBASE,X
                JSR     SETPG
                LDY     #RSX_NEXT
                LDA     #$4C
                JSR     POKEY
                INY
                LDA     TLO
                JSR     POKEY
                INY
                LDA     THI
                JSR     POKEY
                LDA     #RSX_START
                STA     TLO
                LDA     TBASE,X
                STA     THI
                LDY     #RSX_PREV      ; the RSX below's offset 9, or $0203
                LDA     #<BP_BDOS
                STA     TMP
                LDA     #>BP_BDOS
                STA     TMP + 1
                INX
                CPX     NRSX
                BEQ     @prev
                LDA     #RSX_NEXT
                STA     TMP
                LDA     TBASE,X
                STA     TMP + 1
@prev:          LDA     TMP
                JSR     POKEY
                INY
                LDA     TMP + 1
                JSR     POKEY
                BRA     @next
@base:          LDA     #$4C
                STA     BP_BDOS
                LDA     TLO
                STA     BP_TOP
                LDA     THI
                STA     BP_TOP + 1
                RTS

; Out of the table, the rest kept in order, then LINK: every RSX whose offset
; 6 or 9 isn't a JMP (a program wrote over it: so the warm boot can't jump
; into it, ours); and for REMOVE, every RSX whose remove flag is $FF but
; those KEEPNEW kept.
CHECKRSX:       STZ     RMODE
                BRA     DROP
REMOVE:         LDA     #1
                STA     RMODE
DROP:           LDX     #0
                LDY     #0             ; (Y: where the next kept one goes)
@each:          CPX     NRSX
                BEQ     @done
                PHY
                LDA     TBASE,X
                JSR     SETPG
                LDY     #RSX_START
                JSR     PEEKY
                CMP     #$4C
                BNE     @bad
                LDY     #RSX_NEXT
                JSR     PEEKY
                CMP     #$4C
                BNE     @bad
                LDA     RMODE
                BEQ     @keep
                LDA     TKEEP,X
                BNE     @keep
                LDY     #RSX_REMOVE
                JSR     PEEKY
                CMP     #$FF
                BNE     @keep
@bad:           PLY
                BRA     @drop
@keep:          PLY
                LDA     TBASE,X
                STA     TBASE,Y
                LDA     TZP,X
                STA     TZP,Y
                LDA     TZN,X
                STA     TZN,Y
                LDA     TKEEP,X
                STA     TKEEP,Y
                INY
@drop:          INX
                BRA     @each
@done:          STY     NRSX
                JMP     LINK

; The last load's RSXs (LN0 on) kept for the next program; UNKEEP: none.
KEEPNEW:        LDX     LN0
@each:          CPX     NRSX
                BEQ     @done
                LDA     #1
                STA     TKEEP,X
                INX
                BRA     @each
@done:          RTS
UNKEEP:         LDX     #RSX_MAX - 1
@each:          STZ     TKEEP,X
                DEX
                BPL     @each
                STZ     KEEP
                RTS

; ---------------------------------------------------------------------------
; The SCB.

; The SCB entries the last load kept, set by 49, and none kept after.
APPLYSCB:       LDX     #0
@each:          CPX     SCBN
                BEQ     @done
                PHX
                TXA
                ASL     A
                TAX
                LDA     SCBP + 1,X
                PHA
                LDA     SCBP,X
                TAX
                PLA
                JSR     SCBSET
                PLX
                INX
                BRA     @each
@done:          STZ     SCBN
                RTS

; CCPST to the SCB.
CCPSET:         LDA     CCPST
                LDX     #SCB_CCPST
                ; falls through
; SCB byte X = A, by 49.
SCBSET:         STX     SCBPB
                STA     SCBPB + 2
                LDA     #$FF
                STA     SCBPB + 1
                BRA     SCBCALL
; A = SCB byte X, by 49.
SCBGET:         STX     SCBPB
                STZ     SCBPB + 1
SCBCALL:        LDA     #<SCBPB
                LDX     #>SCBPB
                LDY     #S_SCB
                JMP     BDOS_ENTRY

MAGIC:          .byte   OODOS_MAGIC

; GW-BASIC's loader (hw/oodos.md, "The plan, phase 7": the memory layout and
; question 6). The project's own, not Microsoft's.
;
; It runs once, from the end of the resident image (segment LOADER), before
; anything else, and BASIC's workspace starts where it was. In order:
;
;   0. The console's output state cleared (PTRFIL, TTYPOS, LPTPOS: what
;      the previous program left in zero page could send the messages to
;      LST:), and function 80's FT_INFO asked for OA_DMA: an OODOS without
;      function 83 is refused with its own message.
;   1. FARCALL's address, from function 72.
;   2. GWBASIC.OVL opened on the drive GWBASIC.COM came from (the base page's
;      $0250), in the current user, and its header record read and checked:
;      the magic, format 1, 1-8 blocks; each entry's page 1-8 and named
;      once, its first record 1 or more, its length 1-12,288 and its
;      records the length's, rounded up to a record (96 at most).
;   3. A block allocated for each of its blocks (function 55: anywhere in
;      banks 1-5; the warm boot frees them, as it frees any).
;   4. Common memory, $F800-$FFFF, copied by the CPU to a buffer once.
;   5. Each block's image loaded into a buffer by function 83's DM_LOAD, its
;      far call sites patched (FARCALL's address, and each page's block),
;      and the image put at the block's $0800 ($C800 in W3) and common memory
;      at its $3800 ($F800) by DM_COPY. So GW-BASIC copies common memory into
;      its own blocks, as OOUI's ui_load does, and OODOS is unchanged
;      (question 6, decided 2026-11-13).
;   6. The resident core's own far call sites patched.
;
; Nothing the file says is used unchecked (a damaged or hostile GWBASIC.OVL
; must not write outside its own image): a block's site list must lie in
; its image, past the header; each site must have its three bytes (the
; JSR's operand and the page) in the image, past the header; and each
; site's page must be 1-8 and one the file has. The resident core's list is
; the link's, checked there (.assert, below), and its sites are checked the
; same way, against the resident image.
;
; Then INIT. Any failure says why and ends by the warm boot, which frees the
; blocks. The buffers are in the workspace, past this code: nothing of the
; loader is kept.

.include "gwbasic.inc"

.export LOAD, PAGBLK, FCADDR
.import __FARFIX_RUN__, __FARFIX_SIZE__, __FARFIX_LOAD__, __LOADER_RUN__, __BSS_RUN__
.import STROUT, INITSA

LBUF            = __FARFIX_RUN__ + __FARFIX_SIZE__ ; the header record, then the buffers
LHDR            = (LBUF + $FF) & $FF00 ; the OVL's header record (128 bytes)
LCOM            = LHDR + $100          ; common memory's copy, 2 KB
LIMG            = LCOM + $800          ; a block's image, 12 KB at most
; In the header record's page, past the record: transient, as the buffers.
LLO             = LHDR + $80           ; LPATCH: the lowest site, as linked
LHI             = LHDR + $82           ; and the highest (its page byte the image's last)
LTOP            = LHDR + $84           ; a block's image's end, as linked: BLK_BASE + its length
LEND            = LHDR + $86           ; its site list's end
.assert LIMG + $3000 <= $BFFD, error, "the loader's buffers don't fit under the TPA's top"
; The resident core's FARFIX list (step 6): in the image, after the loader,
; before BSS (which isn't in the file), and whole sites.
.assert __FARFIX_LOAD__ = __FARFIX_RUN__, error, "FARFIX isn't where it is loaded"
.assert __FARFIX_RUN__ >= __LOADER_RUN__, error, "FARFIX isn't after the loader"
.assert __FARFIX_RUN__ + __FARFIX_SIZE__ <= __BSS_RUN__, error, "FARFIX isn't in the resident image"
.assert (__FARFIX_SIZE__ & 1) = 0, error, "FARFIX isn't whole sites"
BLK_MAX         = BLK_END - BLK_BASE   ; a block's image: 12,288 bytes at most, 96 records

.segment "LOWRAM"
PAGBLK:         .res    9              ; each page's block (1-8; +0 unused)
FCADDR:         .res    2              ; FARCALL's address

.segment "RAM"
LFCB:           .res    36             ; GWBASIC.OVL's FCB
LVER:           .res    10             ; function 72's answer
LMCB:           .res    5              ; function 55's MCB
LDM:            .res    11             ; function 83's block
LPHYS:          .res    4              ; function 73's answer: the paragraph, the offset
LCOMP:          .res    3              ; LCOM's physical address
LIMGP:          .res    3              ; LIMG's physical address
LN:             .res    1              ; the blocks in the file
LI:             .res    1              ; the block being loaded
LE:             .res    1              ; its header entry's offset
LCNT:           .res    2              ; sites left
LBLK:           .res    1              ; LPATCH: 1 the sites are in LIMG, 0 in memory

.segment "LOADER"

LOAD:           CLD
                ; 0. The output state, and the services: function 83's.
                STZ     PTRFIL         ; the console (OUTDO's, for STROUT)
                STZ     PTRFIL + 1
                STZ     TTYPOS
                STZ     LPTPOS
                LDA     #FT_INFO
                LDX     #0
                LDY     #O_FRAME
                JSR     BDOS
                CMP     #$FF           ; no function 80: A = X = $FF
                BEQ     @old
                TXA
                AND     #OA_DMA
                BNE     @new
@old:           LDA     #<MOLD
                LDX     #>MOLD
                JMP     LFAIL
@new:
                ; 1. FARCALL's address.
                LDA     #<LVER
                LDX     #>LVER
                LDY     #O_VERSION
                JSR     BDOS
                LDA     LVER + 8
                STA     FCADDR
                LDA     LVER + 9
                STA     FCADDR + 1
                ; 2. The file: the drive GWBASIC.COM came from.
                LDX     #35
@clr:           STZ     LFCB,X
                DEX
                BPL     @clr
                LDA     $0250          ; the drive the program came from (hw/oodos.md, "What a program sees")
                STA     LFCB
                LDX     #10
@nm:            LDA     OVLNAM,X
                STA     LFCB + 1,X
                DEX
                BPL     @nm
                LDA     #<LFCB
                LDX     #>LFCB
                LDY     #F_OPEN
                JSR     BDOS
                CMP     #4
                BCC     :+
                LDA     #<MNOOVL
                LDX     #>MNOOVL
                JMP     LFAIL
:               LDA     #<LHDR         ; record 0, the header
                LDX     #>LHDR
                LDY     #F_DMAOFF
                JSR     BDOS
                STZ     LFCB + 33
                STZ     LFCB + 34
                STZ     LFCB + 35
                LDA     #<LFCB
                LDX     #>LFCB
                LDY     #F_READRAND
                JSR     BDOS
                CMP     #0
                JNE     LBAD
                LDX     #3
@mg:            LDA     LHDR,X
                CMP     OVLMAG,X
                JNE     LBAD
                DEX
                BPL     @mg
                LDA     LHDR + 4       ; format 1
                CMP     #1
                JNE     LBAD
                LDA     LHDR + 5       ; 1-8 blocks
                JEQ     LBAD
                CMP     #9
                JCS     LBAD
                STA     LN
                ; 3. A block for each, its entry checked first.
                STZ     LI
@alloc:         JSR     LENTRY
                LDA     LHDR + 2,X     ; its first record: 1 or more (0 is the header)
                ORA     LHDR + 3,X
                JEQ     LBAD
                LDA     LHDR + 7,X     ; its length: 1-12,288 (0 would be a 64 KB DM_COPY)
                CMP     #>BLK_MAX
                BCC     @len
                JNE     LBAD
                LDA     LHDR + 6,X
                JNE     LBAD
@len:           LDA     LHDR + 6,X
                ORA     LHDR + 7,X
                JEQ     LBAD
                LDA     LHDR + 6,X     ; its records: the length's, rounded up
                CLC                    ; ((length + 127) / 128, 96 at most)
                ADC     #127
                STA     T0
                LDA     LHDR + 7,X
                ADC     #0
                ASL     T0
                ROL     A
                CMP     LHDR + 4,X
                JNE     LBAD
                LDA     LHDR + 5,X
                JNE     LBAD
                LDA     LHDR,X         ; its page, 1-8
                JEQ     LBAD
                CMP     #9
                JCS     LBAD
                TAY                    ; and named once: no block for it yet
                LDA     PAGBLK,Y
                JNE     LBAD
                PHY
                STZ     LMCB
                STZ     LMCB + 1
                STZ     LMCB + 2       ; 1,024 paragraphs: one block
                LDA     #>1024
                STA     LMCB + 3
                STZ     LMCB + 4
                LDA     #<LMCB
                LDX     #>LMCB
                LDY     #MC_ALLOC
                JSR     BDOS
                PLY
                CMP     #0
                BEQ     :+
                LDA     #<MNOMEM
                LDX     #>MNOMEM
                JMP     LFAIL
:               LDA     LMCB + 1       ; the block: the paragraph / 1,024
                LSR     A
                LSR     A
                STA     PAGBLK,Y
                INC     LI
                LDA     LI
                CMP     LN
                JNE     @alloc
                ; 4. Common memory to LCOM, by the CPU.
                LDX     #0
@cm:
.repeat 8, k
                LDA     $F800 + k * $100,X
                STA     LCOM + k * $100,X
.endrepeat
                INX
                BNE     @cm
                LDA     #<LCOM
                LDX     #>LCOM
                JSR     LPHYSA
                LDX     #2
@cp:            LDA     LDM + 1,X      ; (LPHYSA leaves it in LDM+1-3)
                STA     LCOMP,X
                DEX
                BPL     @cp
                LDA     #<LIMG
                LDX     #>LIMG
                JSR     LPHYSA
                LDX     #2
@ip:            LDA     LDM + 1,X
                STA     LIMGP,X
                DEX
                BPL     @ip
                ; 5. Each block.
                STZ     LI
@blk:           JSR     LENTRY
                STX     LE
                LDA     #DM_LOAD       ; its records to LIMG
                STA     LDM
                LDA     #<LFCB
                STA     LDM + 1
                LDA     #>LFCB
                STA     LDM + 2
                LDA     LHDR + 2,X
                STA     LDM + 3
                LDA     LHDR + 3,X
                STA     LDM + 4
                STZ     LDM + 5
                LDA     LHDR + 4,X
                STA     LDM + 6
                LDA     LHDR + 5,X
                STA     LDM + 7
                LDX     #2
@d1:            LDA     LIMGP,X
                STA     LDM + 8,X
                DEX
                BPL     @d1
                JSR     LDMA
                CMP     #0
                JNE     LBAD           ; 1, the file ended first: it's short
                LDX     LE             ; the image's end, as linked, and the sites' range:
                CLC                    ; BLK_BASE+4 (past the header) to its end less 3
                LDA     LHDR + 6,X
                ADC     #<BLK_BASE
                STA     LTOP
                LDA     LHDR + 7,X
                ADC     #>BLK_BASE
                STA     LTOP + 1
                SEC
                LDA     LTOP
                SBC     #3
                STA     LHI
                LDA     LTOP + 1
                SBC     #0
                STA     LHI + 1
                LDA     #<(BLK_BASE + 4)
                STA     LLO
                LDA     #>(BLK_BASE + 4)
                STA     LLO + 1
                LDA     LIMG + 2       ; its sites: BLK_FIX's list, in LIMG
                STA     LCNT
                LDA     LIMG + 3
                STA     LCNT + 1
                ORA     LCNT
                BEQ     @nofix
                LDA     LIMG           ; the list in the image, past the header:
                CMP     #<(BLK_BASE + 4) ; BLK_BASE+4 <= its start
                LDA     LIMG + 1
                SBC     #>(BLK_BASE + 4)
                JCC     LBAD
                LDA     LCNT           ; and its end, start + 2 x count, <= LTOP
                ASL     A
                STA     LEND
                LDA     LCNT + 1
                ROL     A
                STA     LEND + 1
                JCS     LBAD
                CLC
                LDA     LEND
                ADC     LIMG
                STA     LEND
                LDA     LEND + 1
                ADC     LIMG + 1
                STA     LEND + 1
                JCS     LBAD
                LDA     LTOP
                CMP     LEND
                LDA     LTOP + 1
                SBC     LEND + 1
                JCC     LBAD
@nofix:         LDA     LIMG           ; the list's place in LIMG
                SEC
                SBC     #<BLK_BASE
                STA     PTR2
                LDA     LIMG + 1
                SBC     #>BLK_BASE
                CLC
                ADC     #>LIMG
                STA     PTR2 + 1
                LDA     #1
                STA     LBLK
                JSR     LPATCH
                LDX     LE             ; the image to the block's $0800
                LDA     #DM_COPY
                STA     LDM
                LDY     #2
@d2:            LDA     LIMGP,Y
                STA     LDM + 1,Y
                DEY
                BPL     @d2
                JSR     LDEST
                LDA     LDM + 5
                ORA     #$08
                STA     LDM + 5
                LDA     LHDR + 6,X
                STA     LDM + 7
                LDA     LHDR + 7,X
                STA     LDM + 8
                JSR     LDMA
                CMP     #0
                JNE     LBAD
                LDA     #DM_COPY       ; and common memory to its $3800
                STA     LDM
                LDY     #2
@d3:            LDA     LCOMP,Y
                STA     LDM + 1,Y
                DEY
                BPL     @d3
                LDX     LE
                JSR     LDEST
                LDA     LDM + 5
                ORA     #$38
                STA     LDM + 5
                STZ     LDM + 7
                LDA     #>$800
                STA     LDM + 8
                JSR     LDMA
                CMP     #0
                JNE     LBAD
                INC     LI
                LDA     LI
                CMP     LN
                JNE     @blk
                ; 6. The resident core's own sites, where they are: each
                ; in the image, before FARFIX.
                LDA     #<(BP_TPA + 1)
                STA     LLO
                LDA     #>(BP_TPA + 1)
                STA     LLO + 1
                LDA     #<(__FARFIX_RUN__ - 3)
                STA     LHI
                LDA     #>(__FARFIX_RUN__ - 3)
                STA     LHI + 1
                LDA     #<__FARFIX_RUN__
                STA     PTR2
                LDA     #>__FARFIX_RUN__
                STA     PTR2 + 1
                LDA     #<(__FARFIX_SIZE__ / 2)
                STA     LCNT
                LDA     #>(__FARFIX_SIZE__ / 2)
                STA     LCNT + 1
                STZ     LBLK
                JSR     LPATCH
                FJSR    GWINIT
                JMP     INITSA         ; the end of start-up (res/bimisc.s)

LBAD:           LDA     #<MBAD
                LDX     #>MBAD
LFAIL:          STA     PTR
                STX     PTR + 1
                JSR     STROUT
                JMP     WBOOT

; X = block LI's entry in LHDR.
LENTRY:         LDA     LI
                ASL     A
                ASL     A
                ASL     A
                CLC
                ADC     #8
                TAX
                RTS

; The block of the entry at X, physical: LDM+4-6 its address (block x
; $4000), the caller adding the page within it to LDM+5. Keeps X.
LDEST:          LDY     LHDR,X         ; the page
                LDA     PAGBLK,Y       ; its block
                STZ     LDM + 4
                PHA
                LSR     A              ; bits 19-16: the block / 4
                LSR     A
                STA     LDM + 6
                PLA
                AND     #3             ; bits 15-14: the block's low 2 bits
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     LDM + 5
                RTS

; Function 83 with LDM. A = its answer.
LDMA:           LDA     #<LDM
                LDX     #>LDM
                LDY     #O_DMA
                JMP     BDOS

; A/X a CPU address: its physical address in LDM+1-3 (by function 73: the
; paragraph x 16 + the offset).
LPHYSA:         STA     LPHYS
                STX     LPHYS + 1
                LDA     #<LPHYS
                LDX     #>LPHYS
                LDY     #O_PHYSADDR
                JSR     BDOS
                LDA     LPHYS + 2      ; the paragraph's low 4 bits, then the offset
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     LPHYS
                STA     LDM + 1
                LDA     LPHYS + 3      ; the paragraph / 16
                STA     LDM + 3
                LDA     LPHYS + 2
                LDX     #4
@sh:            LSR     LDM + 3
                ROR     A
                DEX
                BNE     @sh
                STA     LDM + 2
                RTS

; LCNT sites from the list at PTR2: each the address of a JSR's operand, as
; linked, from LLO to LHI (so the operand and the page byte after it are in
; the image). For a block's (LIMG's list, LBLK set) the address is $C800 on
; and is found in LIMG; for the resident core's (LBLK 0), in memory as it
; is. The page byte must be 1-8 and a page the file has: it becomes its
; block, and FARCALL's address goes in. Anything else is LBAD.
LPATCH:         LDA     LCNT
                ORA     LCNT + 1
                BEQ     @done
                LDA     (PTR2)         ; the site
                STA     PTR
                LDY     #1
                LDA     (PTR2),Y
                STA     PTR + 1
                LDA     PTR            ; LLO <= the site
                CMP     LLO
                LDA     PTR + 1
                SBC     LLO + 1
                BCC     @bad
                LDA     LHI            ; and the site <= LHI
                CMP     PTR
                LDA     LHI + 1
                SBC     PTR + 1
                BCC     @bad
                LDA     LBLK
                BEQ     @mem
                SEC                    ; in LIMG
                LDA     PTR
                SBC     #<BLK_BASE
                STA     PTR
                LDA     PTR + 1
                SBC     #>BLK_BASE
                CLC
                ADC     #>LIMG
                STA     PTR + 1
@mem:           LDY     #2
                LDA     (PTR),Y        ; the page, 1-8
                BEQ     @bad
                CMP     #9
                BCS     @bad
                TAX
                LDA     PAGBLK,X
                BEQ     @bad           ; a page the file hasn't
                STA     (PTR),Y
                LDA     FCADDR
                STA     (PTR)
                LDY     #1
                LDA     FCADDR + 1
                STA     (PTR),Y
                CLC
                LDA     PTR2
                ADC     #2
                STA     PTR2
                BCC     :+
                INC     PTR2 + 1
:               LDA     LCNT
                BNE     :+
                DEC     LCNT + 1
:               DEC     LCNT
                BRA     LPATCH
@done:          RTS
@bad:           JMP     LBAD

.segment "LOADDAT"
MNOOVL:         .byte   "GWBASIC.OVL not found", 13, 10, 0
MNOMEM:         .byte   "No memory for GW-BASIC's blocks", 13, 10, 0
MBAD:           .byte   "GWBASIC.OVL is damaged", 13, 10, 0
MOLD:           .byte   "GW-BASIC needs a newer OODOS", 13, 10, 0
OVLNAM:         .byte   "GWBASIC OVL"
OVLMAG:         .byte   "GWB", $1A

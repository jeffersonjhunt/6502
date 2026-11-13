; GW-BASIC's loader (hw/oodos.md, "The plan, phase 7": the memory layout and
; question 6). The project's own, not Microsoft's.
;
; It runs once, from the end of the resident image (segment LOADER), before
; anything else, and BASIC's workspace starts where it was. In order:
;
;   1. FARCALL's address, from function 72.
;   2. GWBASIC.OVL opened on the drive GWBASIC.COM came from (the base page's
;      $0250), in the current user, and its header record read and checked.
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
; Then INIT. Any failure says why and ends by the warm boot, which frees the
; blocks. The buffers are in the workspace, past this code: nothing of the
; loader is kept.

.include "gwbasic.inc"

.export LOAD, PAGBLK, FCADDR
.import __FARFIX_RUN__, __FARFIX_SIZE__
.import STROUT, READY

LBUF            = __FARFIX_RUN__ + __FARFIX_SIZE__ ; the header record, then the buffers
LHDR            = (LBUF + $FF) & $FF00 ; the OVL's header record (128 bytes)
LCOM            = LHDR + $100          ; common memory's copy, 2 KB
LIMG            = LCOM + $800          ; a block's image, 12 KB at most
.assert LIMG + $3000 <= $BFFD, error, "the loader's buffers don't fit under the TPA's top"

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
                ; 3. A block for each.
                STZ     LI
@alloc:         JSR     LENTRY
                LDA     LHDR,X         ; its page, 1-8
                JEQ     LBAD
                CMP     #9
                JCS     LBAD
                PHA
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
                LDA     LIMG           ; its sites: BLK_FIX's list, in LIMG
                SEC
                SBC     #<BLK_BASE
                STA     PTR2
                LDA     LIMG + 1
                SBC     #>BLK_BASE
                CLC
                ADC     #>LIMG
                STA     PTR2 + 1
                LDA     LIMG + 2
                STA     LCNT
                LDA     LIMG + 3
                STA     LCNT + 1
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
                ; 6. The resident core's own sites, where they are.
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
                JMP     READY

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
; linked. For a block's (LIMG's list, LBLK set) the address is $C800 on and
; is found in LIMG; for the resident core's (LBLK 0), in memory as it is.
; FARCALL's address goes in, and the page byte after it becomes its block.
LPATCH:         LDA     LCNT
                ORA     LCNT + 1
                BEQ     @done
                LDA     (PTR2)         ; the site
                STA     PTR
                LDY     #1
                LDA     (PTR2),Y
                STA     PTR + 1
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
@mem:           LDA     FCADDR
                STA     (PTR)
                LDA     FCADDR + 1
                STA     (PTR),Y
                INY
                LDA     (PTR),Y        ; the page, 1-8
                TAX
                LDA     PAGBLK,X
                BNE     :+
                JMP     LBAD           ; a page the file hasn't
:               STA     (PTR),Y
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

.segment "LOADDAT"
MNOOVL:         .byte   "GWBASIC.OVL not found", 13, 10, 0
MNOMEM:         .byte   "No memory for GW-BASIC's blocks", 13, 10, 0
MBAD:           .byte   "GWBASIC.OVL is damaged", 13, 10, 0
OVLNAM:         .byte   "GWBASIC OVL"
OVLMAG:         .byte   "GWB", $1A

; INITDIR.COM (hw/oodos.md, "The utilities, phase 4b": INITDIR; the CP/M 3
; User's Guide, 5-52): a drive's directory given SFCBs, every fourth entry,
; for date and time stamps; or, with them, their room recovered or their
; stamps cleared.
;
;   INITDIR d:
;
; The directory is read by 17 with '?' (util.inc's UT_SCAN) to see what is
; there; the work is the BIOS's, through function 50, on the directory's
; sectors (SELDSK, SETTRK, SETSEC, SETDMA, SETBNK 1, READ, WRITE), with the
; drive reset (37) before, so the BDOS's buffers are written back, and
; after, so it reads the new directory at its next login. OODOS's DPBs have
; 512-byte sectors, 16 a track (SPT 64, PSH 2: bios.s's DPB macro checks),
; so a sector holds 16 entries, four SFCBs' groups.
;
; Making SFCBs: the entries in an SFCB's place (3, 7, 11, ...) are moved to
; the free entries first, lowest first, in a pass of their own, so a
; failure between leaves two copies of an entry, never none; then each
; SFCB is written, its stamps 0 and its password modes the XFCBs' of the
; files whose extent-zero entries its group has. What is kept in memory
; meanwhile (the entries to move, from the BSS's end up, and the XFCBs'
; users, names and modes, from the TPA's top down) must fit. The move is
; made once without writing first: if the disk hasn't the free entries
; for every one, nothing is written.
;
; A re-format that failed partway (a BIOS error once something was
; written: "Directory partly re-formatted: run INITDIR again.", ours) is
; finished by the next INITDIR: an entry in an SFCB's place with a copy
; already outside them isn't moved again, and a directory whose entry 3
; is an SFCB but another place isn't is one without SFCBs ("Directory
; partly re-formatted.", then the question). A recover is tracked the same
; way: one a write error stopped leaves entry 3 free and later places still
; SFCBs, and the next INITDIR, finding an SFCB in a place but not in entry
; 3, says "Directory partly re-formatted." and asks to recover first (Y
; finishes it; N goes on to the re-format's question).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

E_FREE          = $E5
E_LABEL         = $20
E_SFCB          = $21
XREC            = 13                   ; an XFCB kept: the user, the name, the mode

.zeropage
P:              .res    2              ; an entry
Q:              .res    2              ; a moved entry's copy, an XFCB's record
R:              .res    2              ; an entry an SFCB's slot is for

.code

START:          JSR     UT_INIT
                STZ     MAKING
                STZ     WROTE
                STZ     NOSF
                LDA     #<FCB          ; d: and nothing else
                LDX     #>FCB
                JSR     UT_SPEC
                BCS     @bad
                LDA     FCB
                BEQ     @bad
                LDX     #11
@name:          LDA     FCB,X
                CMP     #' '
                BNE     @bad
                DEX
                BNE     @name
                LDA     #<FCB2
                LDX     #>FCB2
                JSR     UT_SPEC
                BCC     @bad
                CMP     #0
                BNE     @bad
                JSR     UT_OPTS
                CMP     #UT_LNONE
                BEQ     @drive
@bad:           LDA     #<SBADDRV
                LDX     #>SBADDRV
                JMP     FAIL
@drive:         LDA     FCB
                DEC     A
                STA     DRV
                JSR     UT_SELECT
                CMP     #0
                BEQ     @sel
                JMP     QUIT           ; the BDOS said why
@sel:           LDY     #DRV_ROVEC     ; read-only?
                JSR     BDOS
                STA     T
                STX     T + 1
                LDX     DRV
                BEQ     @rotest
@robit:         LSR     T + 1
                ROR     T
                DEX
                BNE     @robit
@rotest:        LDA     T
                LSR     A
                BCC     @rw
                LDA     #<SRO
                LDX     #>SRO
                JMP     FAIL
@rw:            JSR     UT_DPB
                LDA     UT_DPBC + 4    ; EXM: an extent-zero entry's EX has
                EOR     #$1F           ; none of the other bits
                AND     #$1F
                STA     EXMASK
                LDA     UT_DPBC + 7    ; DRM + 1 entries, 16 a sector
                CLC
                ADC     #1
                STA     NENT
                LDA     UT_DPBC + 8
                ADC     #0
                STA     NENT + 1
                LDA     NENT
                LDX     NENT + 1
                LDY     #4
@sec:           STX     T
                LSR     T
                ROR     A
                LDX     T
                DEY
                BNE     @sec
                STA     NSEC           ; (128 at most: 2,048 entries)
                LDA     UT_DPBC + 13   ; OFF, the directory's first track
                STA     OFF
                LDA     UT_DPBC + 14
                STA     OFF + 1
                JSR     UT_SFCBS
                BCS     QUIT
                CMP     #0
                BNE     @have
                INC     NOSF           ; entry 3 not an SFCB
                BRA     MAKE
@have:          JSR     PARTLY
                BCS     @partly
                JMP     HAVE
@partly:        LDA     #<SPARTLY
                LDX     #>SPARTLY
                JSR     UT_SAY
                JMP     MAKE

QUIT:           JSR     UT_FAIL
                JMP     UT_EXIT

; The message at A/X, the return code $FF00, the end.
FAIL:           JSR     UT_SAY
                BRA     QUIT

; ---- a directory without SFCBs -------------------------------------------

MAKE:           JSR     LOOK
                LDA     HALF           ; an SFCB in a place but not in entry 3:
                AND     NOSF           ; a recover a write error stopped
                BEQ     @room0
                LDA     #<SPARTLY
                LDX     #>SPARTLY
                JSR     UT_SAY
                LDA     #<SRECOVER
                LDX     #>SRECOVER
                JSR     ASKQ
                BCC     @room0         ; N: on to the re-format
                JMP     RECOVER
@room0:         LDA     NENT + 1       ; room: in use <= three quarters
                LSR     A
                STA     T + 1
                LDA     NENT
                ROR     A
                LSR     T + 1
                ROR     A              ; a quarter
                STA     T
                SEC
                LDA     NENT
                SBC     T
                STA     T
                LDA     NENT + 1
                SBC     T + 1
                STA     T + 1          ; three quarters
                LDA     T
                CMP     USED
                LDA     T + 1
                SBC     USED + 1
                BCS     @room
                LDA     #<SNOROOM
                LDX     #>SNOROOM
                JMP     FAIL
@room:          LDA     NOMEM
                BEQ     @ask
                LDA     #<SNOMEM
                LDX     #>SNOMEM
                JMP     FAIL
@ask:           LDA     #<SWILL
                LDX     #>SWILL
                JSR     UT_SAY
                LDA     #<SREALLY
                LDX     #>SREALLY
                JSR     CM_PUTS
                LDA     DRV
                CLC
                ADC     #'A'
                JSR     CM_PUTC
                LDA     #<SYN
                LDX     #>SYN
                JSR     ASK
                BCS     @yes
                JMP     UT_EXIT
@yes:           JSR     RESET
                CMP     #0
                BEQ     @reset
                JMP     QUIT           ; the BDOS said why: nothing written
@reset:         LDA     #1
                STA     MAKING
                STA     DRY            ; the first pass once without writing
@pass1:         LDA     UT_MLO         ; the moved entries, from the first
                STA     Q
                LDA     UT_MLO + 1
                STA     Q + 1
                STZ     SECT           ; first pass: each one into a free entry
@apass:         LDA     Q
                CMP     MTOP
                LDA     Q + 1
                SBC     MTOP + 1
                BCS     @placed        ; all moved
                JSR     RDSEC
                STZ     DIRTY
                LDA     #<BUF
                STA     P
                LDA     #>BUF
                STA     P + 1
                LDX     #0             ; the entry in the sector
@aent:          TXA
                AND     #3
                CMP     #3
                BEQ     @anext         ; an SFCB's place
                LDA     (P)
                CMP     #E_FREE
                BNE     @anext
                LDA     Q
                CMP     MTOP
                LDA     Q + 1
                SBC     MTOP + 1
                BCS     @anext
                LDY     #31
@copy:          LDA     (Q),Y
                STA     (P),Y
                DEY
                BPL     @copy
                INC     DIRTY
                JSR     QNEXT32
@anext:         JSR     PNEXT
                INX
                CPX     #16
                BNE     @aent
                LDA     DIRTY
                BEQ     @aon
                LDA     DRY
                BNE     @aon
                JSR     WRSEC
@aon:           INC     SECT
                LDA     SECT
                CMP     NSEC
                BNE     @apass
@placed:        LDA     Q              ; every one placed? Else no SFCB written
                CMP     MTOP
                LDA     Q + 1
                SBC     MTOP + 1
                BCS     @all
                JSR     RESET
                LDA     #<SNOROOM
                LDX     #>SNOROOM
                JMP     FAIL
@all:           LDA     DRY            ; then again, written
                BEQ     @bpass
                STZ     DRY
                JMP     @pass1
@bpass:         STZ     SECT           ; second pass: the SFCBs
@bsec:          JSR     RDSEC
                LDA     #<BUF
                STA     P
                LDA     #>BUF
                STA     P + 1
                LDX     #4             ; four groups a sector
@group:         PHX
                LDY     #96            ; the SFCB: $21, three slots of 10, a 0
                LDA     #E_SFCB
                STA     (P),Y
                LDA     #0
@zero:          INY
                STA     (P),Y
                CPY     #127
                BNE     @zero
                LDA     P              ; each entry's mode into its slot's byte 8
                STA     R
                LDA     P + 1
                STA     R + 1
                STZ     SLOT
@slot:          JSR     SLOTMODE
                PHA
                LDA     SLOT           ; 96 + 1 + slot x 10 + 8
                ASL     A
                STA     T
                ASL     A
                ASL     A
                ADC     T
                ADC     #96 + 1 + 8
                TAY
                PLA
                STA     (P),Y
                LDA     R              ; the group's next entry
                CLC
                ADC     #32
                STA     R
                BCC     @snext
                INC     R + 1
@snext:         INC     SLOT
                LDA     SLOT
                CMP     #3
                BNE     @slot
                LDA     P              ; the next group
                CLC
                ADC     #128
                STA     P
                BCC     @gnext
                INC     P + 1
@gnext:         PLX
                DEX
                BNE     @group
                JSR     WRSEC
                INC     SECT
                LDA     SECT
                CMP     NSEC
                BNE     @bsec
                JMP     DONE

; The entry at R: the password mode its slot gets, A: its XFCB's, for a
; file's extent-zero entry; else 0.
SLOTMODE:       LDA     (R)
                CMP     #16
                BCS     @none
                STA     T
                LDY     #12
                LDA     (R),Y
                AND     EXMASK
                BNE     @none
                LDY     #14
                LDA     (R),Y
                AND     #$3F
                BNE     @none
                LDA     UT_MHI         ; the XFCBs kept, the directory's first
                STA     Q              ; at the top: from there down to XLOW
                LDA     UT_MHI + 1
                STA     Q + 1
@xfcb:          LDA     XLOW
                CMP     Q
                LDA     XLOW + 1
                SBC     Q + 1
                BCS     @none          ; Q at XLOW: none left
                SEC
                LDA     Q
                SBC     #XREC
                STA     Q
                BCS     @rec
                DEC     Q + 1
@rec:           LDA     (Q)
                CMP     T
                BNE     @xfcb
                LDY     #11
@cmp:           LDA     (R),Y
                AND     #$7F
                CMP     (Q),Y
                BNE     @xfcb
                DEY
                BNE     @cmp
                LDY     #12
                LDA     (Q),Y
                RTS
@none:          LDA     #0
                RTS

; The directory by 17: USED its entries in use but SFCBs in their places,
; each other entry in an SFCB's place kept from UT_MLO up (to MTOP), each
; XFCB's user, name and mode from the top down (to XLOW); NOMEM 1 if they
; didn't fit; HALF 1 if a place holds an SFCB. Then DUPS.
LOOK:           JSR     UT_MEM
                LDA     UT_MLO
                STA     MTOP
                LDA     UT_MLO + 1
                STA     MTOP + 1
                LDA     UT_MHI
                STA     XLOW
                LDA     UT_MHI + 1
                STA     XLOW + 1
                STZ     USED
                STZ     USED + 1
                STZ     NOMEM
                STZ     HALF
                JSR     UT_SCAN
@entry:         BCC     @look
                CPX     #0
                BNE     @fail          ; the scan failed: said
                JMP     DUPS
@fail:          JMP     QUIT
@look:          STA     P
                STX     P + 1
                LDA     (P)
                CMP     #E_FREE
                BEQ     @next
                LDA     UT_ENT
                AND     #3
                CMP     #3
                BNE     @used
                LDA     (P)            ; in an SFCB's place: to move
                CMP     #E_SFCB
                BNE     @move
                LDA     #1             ; (an SFCB there stays)
                STA     HALF
                BRA     @x
@move:
                LDA     #32
                JSR     ROOM
                BCS     @used
                LDY     #31
@keep:          LDA     (P),Y
                STA     (Q),Y
                DEY
                BPL     @keep
                CLC
                LDA     MTOP
                ADC     #32
                STA     MTOP
                BCC     @used
                INC     MTOP + 1
@used:          INC     USED
                BNE     @x
                INC     USED + 1
@x:             LDA     (P)            ; an XFCB: kept
                CMP     #16
                BCC     @next
                CMP     #32
                BCS     @next
                LDA     #XREC
                JSR     ROOM
                BCS     @next
                SEC
                LDA     XLOW
                SBC     #XREC
                STA     XLOW
                STA     Q
                LDA     XLOW + 1
                SBC     #0
                STA     XLOW + 1
                STA     Q + 1
                LDA     (P)
                AND     #$0F
                STA     (Q)
                LDY     #11
@name:          LDA     (P),Y
                AND     #$7F
                STA     (Q),Y
                DEY
                BNE     @name
                LDY     #12
                LDA     (P),Y
                AND     #$E0
                STA     (Q),Y
@next:          JSR     UT_SNEXT
                JMP     @entry

; The entries to move that have an exact copy already outside the SFCBs'
; places (left by a re-format that failed partway): taken off the list, and
; off USED.
DUPS:           LDA     MTOP
                CMP     UT_MLO
                BNE     @scan
                LDA     MTOP + 1
                CMP     UT_MLO + 1
                BNE     @scan
                RTS                    ; none to move
@scan:          JSR     UT_SCAN
@entry:         BCC     @look
                CPX     #0
                BEQ     @pack
                JMP     QUIT           ; the scan failed: said
@look:          STA     P
                STX     P + 1
                LDA     (P)
                CMP     #E_FREE
                BEQ     @next
                LDA     UT_ENT
                AND     #3
                CMP     #3
                BEQ     @next
                LDA     UT_MLO
                STA     R
                LDA     UT_MLO + 1
                STA     R + 1
@rec:           LDA     R
                CMP     MTOP
                LDA     R + 1
                SBC     MTOP + 1
                BCS     @next
                LDY     #0             ; from the user and name up: they differ
@cmp:           LDA     (P),Y          ; first, the block pointers' zeros last
                CMP     (R),Y
                BNE     @other
                INY
                CPY     #32
                BNE     @cmp
                LDA     #E_FREE        ; its copy: not moved again
                STA     (R)
                LDA     USED
                BNE     @dec
                DEC     USED + 1
@dec:           DEC     USED
                BRA     @next
@other:         JSR     RNEXT32
                BRA     @rec
@next:          JSR     UT_SNEXT
                BRA     @entry
@pack:          LDA     UT_MLO         ; the list without them
                STA     Q
                STA     R
                LDA     UT_MLO + 1
                STA     Q + 1
                STA     R + 1
@prec:          LDA     R
                CMP     MTOP
                LDA     R + 1
                SBC     MTOP + 1
                BCS     @end
                LDA     (R)
                CMP     #E_FREE
                BEQ     @pnext
                LDY     #31
@pcopy:         LDA     (R),Y
                STA     (Q),Y
                DEY
                BPL     @pcopy
                JSR     QNEXT32
@pnext:         JSR     RNEXT32
                BRA     @prec
@end:           LDA     Q
                STA     MTOP
                LDA     Q + 1
                STA     MTOP + 1
                RTS

; Carry set if an SFCB's place (3, 7, ...) holds anything but an SFCB.
PARTLY:         JSR     UT_SCAN
@entry:         BCC     @look
                CPX     #0
                BNE     @fail
                CLC
                RTS
@fail:          JMP     QUIT           ; the scan failed: said
@look:          STA     P
                STX     P + 1
                LDA     UT_ENT
                AND     #3
                CMP     #3
                BNE     @next
                LDA     (P)
                CMP     #E_SFCB
                BNE     @yes
@next:          JSR     UT_SNEXT
                BRA     @entry
@yes:           SEC
                RTS

; Room for A bytes more between MTOP and XLOW? Carry clear, Q MTOP; carry
; set and NOMEM 1 if not.
ROOM:           CLC
                ADC     MTOP
                STA     T
                LDA     MTOP + 1
                ADC     #0
                STA     T + 1
                LDA     XLOW
                CMP     T
                LDA     XLOW + 1
                SBC     T + 1
                BCC     @full
                LDA     MTOP
                STA     Q
                LDA     MTOP + 1
                STA     Q + 1
                CLC
                RTS
@full:          LDA     #1
                STA     NOMEM
                SEC
                RTS

; ---- a directory with SFCBs ----------------------------------------------

HAVE:           LDA     #<SALREADY
                LDX     #>SALREADY
                JSR     UT_SAY
                LDA     #<SRECOVER
                LDX     #>SRECOVER
                JSR     ASKQ
                BCS     RECOVER
                LDA     #<SCLEAR
                LDX     #>SCLEAR
                JSR     ASKQ
                BCS     @clear
                JMP     UT_EXIT
@clear:         STZ     RECOV
                BRA     GO

; Every SFCB freed, its writes tracked as a re-format's (MAKING): a write
; error once one is made says to run INITDIR again, which finishes it.
RECOVER:        LDA     #1
                STA     RECOV
                STA     MAKING
GO:             JSR     RESET
                CMP     #0
                BEQ     @reset
                JMP     QUIT           ; the BDOS said why: nothing written
@reset:         STZ     SECT
@sec:           JSR     RDSEC
                LDA     #<BUF
                STA     P
                LDA     #>BUF
                STA     P + 1
                LDX     #0
@ent:           LDA     (P)
                CMP     #E_LABEL
                BNE     @sfcb
                LDA     RECOV          ; the label asks for no stamps
                BEQ     @next
                LDY     #12
                LDA     (P),Y
                AND     #$8F
                STA     (P),Y
                BRA     @next
@sfcb:          CMP     #E_SFCB
                BNE     @next
                TXA
                AND     #3
                CMP     #3
                BNE     @next
                LDA     RECOV
                BEQ     @stamps
                LDY     #31            ; recovered: a free entry
                LDA     #E_FREE
@free:          STA     (P),Y
                DEY
                BPL     @free
                BRA     @next
@stamps:        PHX                    ; each slot's 8 bytes of stamps 0, its mode kept
                LDY     #1
@slot:          LDA     #0
                LDX     #8
@zero:          STA     (P),Y
                INY
                DEX
                BNE     @zero
                INY                    ; past the mode and the reserved byte
                INY
                CPY     #31
                BCC     @slot
                PLX
@next:          JSR     PNEXT
                INX
                CPX     #16
                BNE     @ent
                JSR     WRSEC
                INC     SECT
                LDA     SECT
                CMP     NSEC
                BNE     @sec
                JMP     DONE

; ---- the BIOS ------------------------------------------------------------

DONE:           JSR     RESET
                JMP     UT_EXIT

; 37: the drive reset, its buffers written back and dropped.
RESET:          LDA     #1
                STA     T
                STZ     T + 1
                LDX     DRV
                BEQ     @go
@bit:           ASL     T
                ROL     T + 1
                DEX
                BNE     @bit
@go:            LDA     T
                LDX     T + 1
                LDY     #DRV_RESET
                JMP     BDOS

; Sector SECT of the directory into BUF, or from it: "Disk Read Error" or
; "Disk Write Error" (ours) and the end if the BIOS can't (IOERR).
RDSEC:          JSR     PLACE
                LDA     #BIOS::READ
                JSR     BIOS0
                CMP     #0
                BEQ     @ok
                LDA     #<SRDERR
                LDX     #>SRDERR
                JMP     IOERR
@ok:            RTS

WRSEC:          JSR     PLACE
                LDA     #BIOS::WRITE
                STA     PB
                LDA     #1             ; CP/M's deblocking code: a directory write
                STA     PB + 1
                JSR     BIOSPB
                CMP     #0
                BEQ     @ok
                LDA     #<SWRERR
                LDX     #>SWRERR
                JMP     IOERR
@ok:            LDA     MAKING         ; a re-format has written
                STA     WROTE
                RTS

; A BIOS error: the drive reset, the message at A/X; and once a re-format
; has written something, what to do (ours); the end.
IOERR:          PHA
                PHX
                JSR     RESET
                PLX
                PLA
                JSR     UT_SAY
                LDA     WROTE
                BNE     @partly
                JMP     QUIT
@partly:        LDA     #<SAGAIN
                LDX     #>SAGAIN
                JMP     FAIL

; The BIOS at SECT: SELDSK (selected before: no rescan), SETTRK OFF + SECT
; / 16, SETSEC SECT mod 16, SETDMA BUF, SETBNK 1 (the program's).
PLACE:          LDA     #BIOS::SELDSK
                STA     PB
                LDA     DRV
                STA     PB + 1
                LDA     #1
                STA     PB + 2
                JSR     BIOSPB
                LDA     SECT
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                CLC
                ADC     OFF
                STA     PB + 1
                LDA     OFF + 1
                ADC     #0
                STA     PB + 2
                LDA     #BIOS::SETTRK
                STA     PB
                JSR     BIOSPB
                LDA     SECT
                AND     #15
                STA     PB + 1
                STZ     PB + 2
                LDA     #BIOS::SETSEC
                STA     PB
                JSR     BIOSPB
                LDA     #<BUF
                STA     PB + 1
                LDA     #>BUF
                STA     PB + 2
                LDA     #BIOS::SETDMA
                STA     PB
                JSR     BIOSPB
                LDA     #1
                STA     PB + 1
                LDA     #BIOS::SETBNK
                ; fall through

; Function 50: BIOS function A, or PB as it is (BIOSPB).
BIOS0:          STA     PB
BIOSPB:         LDA     #<PB
                LDX     #>PB
                LDY     #S_BIOS
                JMP     BDOS

; ---- small things --------------------------------------------------------

; The question at A/X, then a line: carry set if it starts with Y.
ASKQ:           JSR     CM_PUTS
                LDA     #<SYN
                LDX     #>SYN
ASK:            JSR     CM_PUTS
                JMP     CM_ASK

PNEXT:          LDA     P
                CLC
                ADC     #32
                STA     P
                BCC     @done
                INC     P + 1
@done:          RTS

QNEXT32:        LDA     Q
                CLC
                ADC     #32
                STA     Q
                BCC     @done
                INC     Q + 1
@done:          RTS

RNEXT32:        LDA     R
                CLC
                ADC     #32
                STA     R
                BCC     @done
                INC     R + 1
@done:          RTS

        .include "cmd.inc"
        .include "util.inc"

        .rodata
SBADDRV:        .byte   "Invalid drive.$"
SRO:            .byte   "Drive Read Only$"
SNOROOM:        .byte   "Not enough room in directory.$"
SNOMEM:         .byte   "Not Enough Memory$"
SWILL:          .byte   "INITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.$"
SREALLY:        .byte   "Do you really want to re-format the directory: $"
SYN:            .byte   " (Y/N)? $"
SALREADY:       .byte   "Directory already re-formatted.$"
SRECOVER:       .byte   "Do you wish to recover date/time directory space$"
SCLEAR:         .byte   "Do you want the existing date/time stamps cleared$"
SRDERR:         .byte   "Disk Read Error$"
SWRERR:         .byte   "Disk Write Error$"
SPARTLY:        .byte   "Directory partly re-formatted.$"
SAGAIN:         .byte   "Directory partly re-formatted: run INITDIR again.$"

        .bss
FCB:            .res    36
FCB2:           .res    36
DRV:            .res    1
EXMASK:         .res    1
NENT:           .res    2              ; the directory's entries
NSEC:           .res    1              ; its sectors
OFF:            .res    2              ; its first track
USED:           .res    2              ; entries in use
NOMEM:          .res    1
MTOP:           .res    2              ; past the moved entries' copies
XLOW:           .res    2              ; the XFCBs' records, from here to UT_MHI
SECT:           .res    1
SLOT:           .res    1
DIRTY:          .res    1
RECOV:          .res    1
DRY:            .res    1              ; 1: the first pass not written
MAKING:         .res    1              ; 1: SFCBs being made or recovered
WROTE:          .res    1              ; 1: and something written
NOSF:           .res    1              ; 1: entry 3 not an SFCB
HALF:           .res    1              ; 1: a place holds an SFCB
T:              .res    2
PB:             .res    8              ; function 50's parameter block
BUF:            .res    512            ; a directory sector

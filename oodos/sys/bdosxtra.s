; OODOS's BDOS, phase 3b1's file extras (hw/oodos.md, "The BDOS, phase 3"):
; the multi-sector count, directory labels, XFCBs and passwords. Block 60,
; with bdos.s, bdosfile.s and bdosdisk.s. Written from the CP/M 3
; Programmer's Guide's descriptions (2.3.6-2.3.8, 2.3.10, functions 44 and
; 100-106); where it is silent the choice is ours, in hw/oodos.md.
;
;   44 F_MULTISEC    the multi-sector count = A, 1-128 (the SCB's, $4A); else
;                    A = $FF, the count kept. bdosfile.s's MULTI uses it
;  100 DRV_SETLABEL  the label of the FCB's drive made or changed: the FCB's
;                    name and type, its byte 12's bits 7-4 (bit 0 set, the
;                    label's there); with bit 0 a new password, the DMA
;                    buffer's bytes 8-15. A label's own password must be
;                    given. Stamps asked for without SFCBs, or no free entry
;                    for a new label: A = $FF. Its update stamped, and its
;                    creation when made. A = 0
;  101 DRV_GETLABEL  A = drive A's label's byte 12, 0 without one
;  102 F_TIMEDATE    the FCB's byte 12 = its file's XFCB's mode (0 none),
;                    bytes 24-31 its extent-zero entry's stamps (0 none). A
;                    = 0, $FF no file
;  103 F_WRITEXFCB   the file's XFCB made or changed: the mode FCB byte 12's
;                    bits 7-5, with bit 0 a new password, the DMA buffer's
;                    bytes 8-15. A protected file's password must be given.
;                    A = 0; $FF no label with passwords on, no file, no room
;  106 F_PASSWD      the default password = the 8 bytes at A/X
;
; Passwords: on a drive whose label's bit 7 (MODE) is set, a file is
; protected when its XFCB (user + 16, the file's name) has any of the mode
; bits 7-5, unless its password is given: the DMA buffer's first 8 bytes,
; or the default password, match it. A password of 8 blanks or 8 zero bytes
; is none, which anything matches. On the disk the password is CP/M 3's (as
; cpmtools reads it): byte 13 the key, the sum of the 8 bytes; bytes 16-23
; the bytes in reverse order, each XORed with the key. The XFCB's mode is
; also kept in the file's extent-zero SFCB slot, its byte 8 (SLOTF).

.setcpu "65C02"
.include "bdos.inc"

.zeropage
PKEY:           .res    1              ; a password's key
XMODE:          .res    1              ; an XFCB's mode
XDID:           .res    2              ; an XFCB's or the label's entry; high $FF none
LNEW:           .res    1              ; F_SETLBL: 1 a new label

.segment "SYSBSS"
DEFPW:          .res    PW_LEN         ; 106's: none (zeros) from the cold boot
PWB:            .res    2 * PW_LEN     ; the DMA buffer's first 16 bytes
PWE:            .res    PW_LEN         ; an entry's password, decoded
XSAVE:          .res    17             ; XCLEAR's SPAT, SMODE and DIDX

.segment "BDOS"

; ---------------------------------------------------------------------------
; 44, 101, 106

F_MULTI:        DEC     A
                CMP     #128
                BCS     @no
                INC     A
                STA     SCB_MULTI
                JMP     RET0
@no:            LDA     #$FF
                JMP     RETA

F_GETLBL:       CMP     #16
                BCC     @drive
                STA     EDRV
                LDA     #ER_SELECT
                JMP     DERR
@drive:         JSR     SELECT
                LDA     MODE
                JMP     RETA

F_DEFPW:        LDA     PA
                STA     BW1
                LDA     PX
                STA     BW1 + 1
                LDA     #<DEFPW
                STA     BW2
                LDA     #>DEFPW
                STA     BW2 + 1
                LDA     #PW_LEN
                JSR     PIN
                JMP     RET0

; ---------------------------------------------------------------------------
; 100: the label

F_SETLBL:       LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     RODRV
                LDA     FC + FCB_EX     ; stamps need SFCBs: entry 3 one
                AND     #MODE_CREATE | MODE_UPDATE | MODE_ACCESS
                BEQ     @find
                LDA     #3
                STA     DIDX
                STZ     DIDX + 1
                JSR     DIRENT
                LDA     (DPTR)
                CMP     #DIR_SFCB
                BEQ     @find
                JMP     @no
@find:          JSR     LFIND
                BCS     @new
                JSR     XKEEP
                JSR     PWDEC          ; its password must be given
                JSR     PWOK
                BCC     @given
                LDA     #ER_PASSWORD
                JMP     DERR
@given:         JSR     XBACK
                STZ     LNEW
                BRA     @put
@new:           JSR     FINDFREE
                BCS     @no
                LDY     #FCB_S1
                LDA     #0
@zero:          STA     (DPTR),Y
                INY
                CPY     #ENTRY_LEN
                BNE     @zero
                JSR     PWBLANK
                LDA     #1
                STA     LNEW
@put:           LDA     FC + FCB_EX     ; bit 0: a new password, the DMA's 8-15
                LSR     A
                BCC     @pw
                JSR     PWGET
                LDX     #PW_LEN
                JSR     PWTAKE
@pw:            LDA     #DIR_LABEL
                STA     (DPTR)
                LDY     #FCB_NAME
@name:          LDA     FC,Y
                STA     (DPTR),Y
                INY
                CPY     #FCB_EX
                BNE     @name
                LDA     FC + FCB_EX
                AND     #$F0
                ORA     #1
                STA     (DPTR),Y
                STA     MODE
                JSR     PWENC
                JSR     NOW
                LDA     LNEW
                BEQ     @update
                LDY     #24            ; made: its creation
                JSR     PUTNOW
@update:        LDY     #28
                JSR     PUTNOW
                JSR     DIRPUT
                JSR     HWMUP
                JSR     SAVECD         ; the drive's modes from now on
                JMP     RET0
@no:            LDA     #$FF
                JMP     RETA

; The label: carry clear, DIDX and DPTR at it; carry set if there's none.
LFIND:          LDA     #DIR_LABEL
                STA     SPAT
                LDX     #FCB_EX - 1
                LDA     #'?'
@any:           STA     SPAT,X
                DEX
                BNE     @any
                LDA     #SM_ANYNAME | SM_ANYEX
                STA     SMODE
                JSR     PATDERIV
                STZ     DIDX
                STZ     DIDX + 1
                JMP     FIND

; ---------------------------------------------------------------------------
; 102, 103: XFCBs

F_STAMPS:       LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     NOWILD
                LDA     SCB_USER       ; the file, in any extent
                LDX     #SM_ANYEX
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCS     @none
                STZ     FC + FCB_EX     ; its XFCB's mode, 0 without one
                LDA     SCB_USER
                JSR     XFIND
                BCS     @stamps
                LDY     #FCB_EX
                LDA     (DPTR),Y
                STA     FC + FCB_EX
@stamps:        LDX     #7             ; its extent-zero entry's stamps, 0 without
@clear:         STZ     FC + 24,X
                DEX
                BPL     @clear
                LDA     SCB_USER
                LDX     #0
                JSR     SETPAT
                JSR     EXT0
                BCS     @out
                JSR     SFCBAT
                BCS     @out
                LDY     STOFS
                LDX     #0
@copy:          LDA     (DPTR),Y
                STA     FC + 24,X
                INY
                INX
                CPX     #8
                BNE     @copy
                JSR     SFREST
@out:           JSR     FCBOUT
                JMP     RET0
@none:          LDA     #$FF
                JMP     RETA

F_WXFCB:        LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     NOWILD
                JSR     RODRV
                BIT     MODE           ; passwords on (and so a label)
                BPL     @no
                LDA     SCB_USER       ; the file, in any extent
                LDX     #SM_ANYEX
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCS     @no
                LDA     SCB_USER
                JSR     XFIND
                BCS     @new
                JSR     XKEEP
                JSR     PWDEC          ; the old password, kept without bit 0
                LDY     #FCB_EX
                LDA     (DPTR),Y
                AND     #XFCB_MODES
                BEQ     @given         ; not protected
                JSR     PWOK
                BCC     @given
                LDA     #ER_PASSWORD
                JMP     DERR
@given:         JSR     XBACK
                BRA     @put
@new:           JSR     FINDFREE
                BCS     @no
                JSR     PWBLANK
@put:           LDA     FC + FCB_EX     ; bit 0: a new password, the DMA's 8-15
                LSR     A
                BCC     @mode
                JSR     PWGET
                LDX     #PW_LEN
                JSR     PWTAKE
@mode:          LDA     FC + FCB_EX
                AND     #XFCB_MODES
                STA     XMODE
                JSR     XPUT
                LDA     SCB_USER
                JSR     SLOTF
                JMP     RET0
@no:            LDA     #$FF
                JMP     RETA

; XDID = DIDX; XBACK puts DIDX and DPTR back there.
XKEEP:          LDA     DIDX
                STA     XDID
                LDA     DIDX + 1
                STA     XDID + 1
                RTS
XBACK:          LDA     XDID
                STA     DIDX
                LDA     XDID + 1
                STA     DIDX + 1
                JMP     DIRENT

; The XFCB of user A's file F names: carry clear, DIDX and DPTR at it;
; carry set if it has none.
XFIND:          ORA     #16
                LDX     #SM_ANYEX
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JMP     FIND

; The XFCB at DPTR: the current user's, F's name, XMODE, the password PWE;
; written, the high-water mark moved.
XPUT:           LDA     SCB_USER
                ORA     #16
                STA     (DPTR)
                LDY     #FCB_NAME
@name:          LDA     FC,Y
                AND     #$7F
                STA     (DPTR),Y
                INY
                CPY     #FCB_EX
                BNE     @name
                LDA     XMODE
                STA     (DPTR),Y
                LDA     #0
@zero:          INY
                STA     (DPTR),Y
                CPY     #ENTRY_LEN - 1
                BNE     @zero
                JSR     PWENC
                JSR     DIRPUT
                JMP     HWMUP

; Make (22) with f6' and passwords on: room for the file's entry and its
; XFCB (an XFCB of the name, XDID, or a second free entry); carry set if
; there's none.
MKROOM:         LDA     SCB_USER
                JSR     XFIND
                BCS     @free
                JMP     XKEEP          ; carry clear
@free:          LDA     #$FF
                STA     XDID + 1
                JSR     FINDFREE
                BCS     @full
                JMP     FINDNXT        ; SM_FREE still
@full:          RTS

; Make's XFCB, after the file's entry: the DMA buffer's bytes 0-7 the
; password, byte 8 the mode; its slot's mode byte.
MKXFCB:         JSR     PWGET
                LDA     PWB + PW_LEN
                AND     #XFCB_MODES
                STA     XMODE
                LDX     #0
                JSR     PWTAKE
                LDA     XDID + 1
                CMP     #$FF
                BEQ     @new
                JSR     XBACK
                BRA     @put
@new:           JSR     FINDFREE       ; MKROOM saw it
@put:           JSR     XPUT
                LDA     SCB_USER
                JMP     SLOTF

; XMODE into the SFCB slot (byte 8) of user A's file F names, its
; extent-zero entry, if it has one.
SLOTF:          LDX     #0
                JSR     SETPAT
SLOTX:          JSR     EXT0
                BCS     @none
                JSR     SFCBAT
                BCS     @none
                LDA     STOFS
                CLC
                ADC     #8
                TAY
                LDA     XMODE
                STA     (DPTR),Y
                JMP     SFCBPUT
@none:          RTS

; FIND's pattern (SPAT's user and name) at extent 0, S2 0, from entry 0.
EXT0:           STZ     SPAT + 12
                STZ     SPAT + 13
                STZ     SMODE
                JSR     PATDERIV
                STZ     DIDX
                STZ     DIDX + 1
                JMP     FIND

; Delete with f5' (the XFCBs only): the XFCB at DPTR, being deleted, leaves
; its file unprotected, so the file's slot's mode byte is 0. FIND's pattern
; and place kept.
XCLEAR:         LDX     #13
@save:          LDA     SPAT,X
                STA     XSAVE,X
                DEX
                BPL     @save
                LDA     SMODE
                STA     XSAVE + 14
                LDA     DIDX
                STA     XSAVE + 15
                LDA     DIDX + 1
                STA     XSAVE + 16
                LDA     (DPTR)
                AND     #$0F
                STA     SPAT
                LDY     #FCB_NAME
@name:          LDA     (DPTR),Y
                AND     #$7F
                STA     SPAT,Y
                INY
                CPY     #FCB_EX
                BNE     @name
                STZ     XMODE
                JSR     SLOTX
                LDX     #13
@back:          LDA     XSAVE,X
                STA     SPAT,X
                DEX
                BPL     @back
                LDA     XSAVE + 14
                STA     SMODE
                JSR     PATDERIV
                LDA     XSAVE + 15
                STA     DIDX
                LDA     XSAVE + 16
                STA     DIDX + 1
                JMP     DIRENT

; ---------------------------------------------------------------------------
; Passwords

; Carry clear if user A's file F names is unprotected, or its password is
; given; carry set, A its XFCB's mode, if not. DIDX and DPTR move.
PWFILE:         BIT     MODE
                BPL     @open          ; passwords off
                JSR     XFIND
                BCS     @open
                LDY     #FCB_EX
                LDA     (DPTR),Y
                AND     #XFCB_MODES
                BEQ     @open
                STA     XMODE
                JSR     PWDEC
                JSR     PWOK
                LDA     XMODE
                RTS
@open:          CLC
                RTS

; Password Error if an XFCB FIND's pattern matches is protected and its
; password not given (19, 23: their patterns take XFCBs too). DIDX moves.
PWALL:          BIT     MODE
                BPL     @done
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                LDA     (DPTR)
                CMP     #16
                BCC     @next          ; a file's entry
                LDY     #FCB_EX
                LDA     (DPTR),Y
                AND     #XFCB_MODES
                BEQ     @next
                JSR     PWDEC
                JSR     PWOK
                BCC     @next
                LDA     #ER_PASSWORD
                JMP     DERR
@next:          JSR     FINDNXT
                BRA     @entry
@done:          RTS

; PWE = the password of the entry at DPTR (an XFCB or the label).
PWDEC:          LDY     #FCB_S1
                LDA     (DPTR),Y
                STA     PKEY
                LDX     #0
                LDY     #23
@byte:          LDA     (DPTR),Y
                EOR     PKEY
                STA     PWE,X
                DEY
                INX
                CPX     #PW_LEN
                BNE     @byte
                RTS

; PWE into the entry at DPTR, as CP/M 3 keeps it: the key at 13. None is
; kept as 8 blanks (fsck.cpm wants a printable password).
PWENC:          JSR     PWNONE
                BCC     @sum0
                JSR     PWBLANK
@sum0:          LDA     #0
                LDX     #PW_LEN - 1
@sum:           CLC
                ADC     PWE,X
                DEX
                BPL     @sum
                STA     PKEY
                LDY     #FCB_S1
                STA     (DPTR),Y
                LDX     #0
                LDY     #23
@byte:          LDA     PWE,X
                EOR     PKEY
                STA     (DPTR),Y
                DEY
                INX
                CPX     #PW_LEN
                BNE     @byte
                RTS

; Carry set if PWE is none: 8 blanks, or 8 zero bytes.
PWNONE:         LDA     PWE
                CMP     #' '
                BEQ     @same
                CMP     #0
                BNE     @some
@same:          LDX     #PW_LEN - 1
@byte:          CMP     PWE,X
                BNE     @some
                DEX
                BNE     @byte
                SEC
                RTS
@some:          CLC
                RTS

; Carry clear if PWE is given: none, or the DMA buffer's first 8 bytes, or
; the default password.
PWOK:           JSR     PWNONE
                BCS     @yes
                JSR     PWGET
                LDX     #PW_LEN - 1
@dma:           LDA     PWB,X
                CMP     PWE,X
                BNE     @default
                DEX
                BPL     @dma
                BRA     @yes
@default:       LDX     #PW_LEN - 1
@def:           LDA     DEFPW,X
                CMP     PWE,X
                BNE     @no
                DEX
                BPL     @def
@yes:           CLC
                RTS
@no:            SEC
                RTS

; PWB = the DMA buffer's first 16 bytes. Keeps W2 (the jump table's).
PWGET:          LDA     SCB_DMA
                STA     BW1
                LDA     SCB_DMA + 1
                STA     BW1 + 1
                LDA     #<PWB
                STA     BW2
                LDA     #>PWB
                STA     BW2 + 1
                LDA     #2 * PW_LEN
                JMP     PIN

; PWE = PWB's 8 bytes from X; PWBLANK: 8 blanks.
PWTAKE:         LDY     #0
@byte:          LDA     PWB,X
                STA     PWE,Y
                INX
                INY
                CPY     #PW_LEN
                BNE     @byte
                RTS
PWBLANK:        LDA     #' '
                LDX     #PW_LEN - 1
@byte:          STA     PWE,X
                DEX
                BPL     @byte
                RTS

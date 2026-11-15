; The OEM layer's part in block P4 (the project's own; hw/oodos.md, "The
; plan, phase 7": 7c, and the owner's decision of 2026-11-13): the directory
; hook, and (the devices step) COM1's routines on AUX: (below). OODOS has no
; directories, so CHDIR, MKDIR and RMDIR (GWSTS.ASM's,
; Advanced Feature in the 1983 source, which has no word for them) all come
; here, and a later OODOS directory call replaces only this routine:
;
;   DIRHK   X 0 CHDIR, 1 MKDIR, 2 RMDIR; the path a string formula (a number
;           is Type mismatch, as NAMFIL's names are). CHDIR to "\", "." or ""
;           (the root, the only directory) is done; any other CHDIR and any
;           RMDIR are Path not found (76); MKDIR is Path/File access error
;           (75), as GW-BASIC gives them under MS-DOS.
;
; Their stubs are in the home block (p8/far4.s), as STMDSX's statements' are.
; DIRHK returns by RTS, or ends in an error.

.include "gwbasic.inc"

;@entry DIRHK
.export DIRHK
.import FRMEVL, FRESTR, ERROR

ERRPAE          = 75                   ; Path/File access error
ERRPNF          = 76                   ; Path not found

.code

DIRHK:          PHX
                JSR     FRMEVL         ; the path
                JSR     FRESTR         ; a string, its temporary freed; RBL its descriptor
                PLX
                BNE     NOTCHD
                LDA     (RBL)          ; CHDIR: "" is the root
                BEQ     DIROK
                CMP     #1
                BNE     ERPNF
                LDY     #1
                LDA     (RBL),Y
                STA     PTR
                INY
                LDA     (RBL),Y
                STA     PTR+1
                LDA     (PTR)
                CMP     #$5C           ; "\"
                BEQ     DIROK
                CMP     #'.'
                BEQ     DIROK
ERPNF:          LDA     #ERRPNF
                JMP     ERROR
NOTCHD:         DEX                    ; RMDIR
                BNE     ERPNF
                LDA     #ERRPAE        ; MKDIR
                JMP     ERROR
DIROK:          RTS

; --- COM1's OEM routines (GIOCOM.ASM's INICOM, SNDCOM, RECCOM, STACOM,
; TRMCOM; the 1983 source has none: the project's own) -----------------------
;
; COM1 is AUX:, polled (hw/oodos.md, decision 12): the BDOS's functions 3,
; 4 and 7. X the unit (0, COM1: COM2 has no driver); each returns A its
; error, 0, as the BDOS's AUX: functions have none (CKCMER, p4/giocom.s).
;
;   INICOM  the DCB's options checked: the speed one of the PC's (75 to
;           9,600) or the OO-1 line's own, 19,200; else A $FF, Bad file
;           name. OO-1 (the owner, 2026-11-14): nothing is set. OODOS's
;           DEVINI ignores the device table's speed and TTY has no soft
;           speed, so the line stays 19,200, 8-N-1, whatever was asked:
;           the speed, parity, data and stop bits are kept in the DCB only
;   SNDCOM  A out, by function 4 (which waits for the line)
;   RECCOM  C set and A the byte if one waits (function 7, then 3); else
;           C clear
;   STACOM  DX the bytes waiting, 0 or 1 (function 7: no receive buffer,
;           the line holds one), CX the room left in that one place
;   TRMCOM  the channel's end: nothing to do

.export INICOM, SNDCOM, RECCOM, STACOM, TRMCOM
.import CMBAUD

NSPEED          = 11

INICOM:         LDX     #0
@l:             LDA     SPEEDS,X
                CMP     CMBAUD
                BNE     @n
                LDA     SPEEDS+1,X
                CMP     CMBAUD+1
                BEQ     @ok
@n:             INX
                INX
                CPX     #2 * NSPEED
                BNE     @l
                LDA     #$FF           ; Bad file name
                RTS
@ok:            LDA     #0
                RTS

SNDCOM:         LDY     #A_WRITE
                JSR     BDOS
                LDA     #0
                RTS

RECCOM:         LDY     #A_STATIN
                JSR     BDOS
                CMP     #0
                CLC
                BEQ     @x             ; none waits
                LDY     #A_READ
                JSR     BDOS
                SEC
@x:             RTS

STACOM:         LDY     #A_STATIN
                JSR     BDOS
                STZ     RDH            ; DX: waiting
                STZ     RCH
                LDX     #0
                CMP     #0
                BEQ     :+
                INX
:               STX     RDL
                TXA                    ; CX: 1 - DX
                EOR     #1
                STA     RCL
TRMCOM:         LDA     #0
                RTS

.rodata
SPEEDS:         .word   75, 110, 150, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200

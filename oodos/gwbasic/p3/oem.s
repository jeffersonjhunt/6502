; The OEM layer's part in block P3 (the project's own; hw/oodos.md, "The
; plan, phase 7": 7c, and the owner's decision of 2026-11-13): the directory
; hook, moved from P4 by the IBM spec's fix-up (review round 1, P4's room).
; OODOS has no directories, so CHDIR, MKDIR and RMDIR (GWSTS.ASM's, Advanced
; Feature in the 1983 source, which has no word for them) all come here, and
; a later OODOS directory call replaces only this routine:
;
;   DIRHK   X 0 CHDIR, 1 MKDIR, 2 RMDIR; the path a string formula (a number
;           is Type mismatch, as NAMFIL's names are). CHDIR to "\", "." or ""
;           (the root, the only directory) is done; any other CHDIR and any
;           RMDIR are Path not found (76); MKDIR is Path/file access error
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

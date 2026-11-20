; WINDOW's map, PMAP and POINT(2) and (3), in block P1 (hw/oodos.md, "The
; plan, phase 7": 7e4; the space plan's answer 6: the math block): the
; OO-1's, written from the IBM manual (pp. 256-260, 370-374), the source
; having no code for them (GWSTS.ASM's WINDOW and PMAP fall into ADVERR).

.include "gwbasic.inc"

;@entry WINSET
;@entry WINRSC
;@entry WINXY
;@entry PMAPP
;@entry WPNT
;@entry WINRAD
.export WINSET, WINRSC, WINXY, PMAPP, WPNT, WINRAD
.import ADVERR

.code

WINSET:
WINXY:
PMAPP:
WPNT:
WINRAD:         JMP     ADVERR
WINRSC:         RTS

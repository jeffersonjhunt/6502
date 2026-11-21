; The home block's stubs for 7f's statements (hw/oodos.md, "The plan, phase
; 7": 7f, sound, input and events; block P7's), the project's own. In a file
; of its own, the home block's last, so its entry comes after the others
; (tools/gwgen.py numbers them by file, then by line):
;
;   HPENST    PEN and STRIG as statements (the resident NEWSTX's, TXTPTR at
;             the function's byte): U1 a stub, Advanced Feature as before

.include "gwbasic.inc"

;@entry HPENST
.export HPENST
.import ADVERR

.code

HPENST:         JMP     ADVERR

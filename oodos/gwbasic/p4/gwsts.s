; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWSTS.ASM's PUT and GET, translated, in block P4, the file layer
; (hw/oodos.md, "The plan, phase 7": 7c):
;
;   PUTP GETP       PUT and GET: a "(" or "@" after the word is the graphics
;                   statement's (GPUTG, 7e's: Advanced Feature until then);
;                   anything else is a file's (DPUTG, p4/gio86.s)
;
; Their stubs are in the home block (p8/far4.s), as STMDSX's statements'
; are. A is the character after the word, CHRGTR's (FARCALL keeps it).

.include "gwbasic.inc"

;@entry PUTP
;@entry GETP
.export PUTP, GETP
.import DPUTG, ADVERR

.code

; PUT, GET (GWSTS.ASM 404): X the PUT flag (1) or GET's (0), the 8086's CX.
PUTP:           LDX     #1
                BRA     PARSE
GETP:           LDX     #0
PARSE:          CMP     #'('
                BEQ     GRPVER
                CMP     #'@'
                BNE     DSKVER
GRPVER:         JMP     ADVERR         ; GPUTG: 7e's
DSKVER:         JMP     DPUTG

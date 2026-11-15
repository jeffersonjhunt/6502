; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; FIVEO.ASM's variables for the home block's WHILE and WEND (p8/fiveo.s): resident, as
; the home block reaches fixed RAM at any time (hw/oodos.md, "The plan, phase 7": 7c's home block).

.include "gwbasic.inc"

.export WHLTXT

.segment "RAM"
WHLTXT:         .res    2              ; WHILE's formula (the 8086's ENDFOR)

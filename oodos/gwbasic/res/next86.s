; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; NEXT86.ASM's variables for the home block's NEXT (p8/next86.s): resident, as
; the home block reaches fixed RAM at any time (hw/oodos.md, "The plan, phase 7": 7c's home block).

.include "gwbasic.inc"

.export NXTFLG, NXTTXT, NXTTMP, NXTVAR

.segment "RAM"
; GWDATA.ASM's
NXTFLG:         .res    1              ; 0: FOR's first test; else NEXT's
NXTTXT:         .res    2              ; where this NEXT's variable starts
NXTTMP:         .res    2              ; the text after it
NXTVAR:         .res    2              ; the loop variable

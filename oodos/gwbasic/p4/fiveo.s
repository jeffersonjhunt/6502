; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; What block P4 keeps of FIVEO.ASM's CHAIN (hw/oodos.md, "The plan, phase 7":
; 7c; F3's M4 moved CHAIN itself, CHAINP to CHNRET, to the home block's
; p8/chain.s, which reaches these by far calls):
;
;   CHNSTP          step 6's way on (FIVEO.ASM's NTMDLT): BX, the old FRETOP,
;                   kept in SAVFRE; then X 0, CHAIN MERGE's (OKGETM), or X 1,
;                   CHNENT; X back as it came, for the stub
;   SAVFGT          step 7's (CHNRET's): BX SAVFRE
;   CHAINR, CHNRTR  CHAINP's and CHNRET's jump table slots, retired (an
;                   RTS), so P4's later entries keep their places
;
; SAVFRE stays in P4's RAM because XFRSTR (p4/gio86.s) moves it with the
; string space when a FIELD buffer is taken or given back, as the source's
; does. PRGFLI, CHNSTP and SAVFGT are P4's last entries (p4/scnedt.s).

.include "gwbasic.inc"

;@entry CHAINR
;@entry CHNRTR
.export SAVFRE, CHNSTP, SAVFGT, CHAINR, CHNRTR
.import OKGETM, CHNENT

.code

CHNSTP:         LDA     RBL            ; SAVFRE: the old FRETOP
                STA     SAVFRE
                LDA     RBH
                STA     SAVFRE+1
                CPX     #0             ; MERGE?
                BNE     :+
                JSR     OKGETM
                LDX     #0
                RTS
:               JSR     CHNENT
                LDX     #1
                RTS

SAVFGT:         LDA     SAVFRE
                STA     RBL
                LDA     SAVFRE+1
                STA     RBH
CHAINR:
CHNRTR:         RTS

.segment "BRAM"
SAVFRE:         .res    2              ; FRETOP before CHAIN moved the variables up

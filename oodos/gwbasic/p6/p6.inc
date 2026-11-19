; Block P6's shared names (hw/oodos.md, "The plan, phase 7": 7d1, the text
; screen): the project's own. P6 is GW-BASIC's screen: SCNDRV.ASM's driver
; and editor (p6/scndrv.s), SCNEDT.ASM's INLIN (p6/scnedt.s), the OEM's text
; routines on the card's port with a one-row cache (p6/oem.s), and the
; keyboard from the event queue (p6/key.s). Its variables are its own RAM
; (BRAM), never zero page: TXRUN runs from OUTDO, anywhere, so P6 touches no
; byte of zero page the interpreter may be using.
;
; The 8086's registers, where SCNDRV.ASM keeps its state in them, are bytes
; of P6's RAM of the same names: xAL xAH (AX), xBL xBH, xCL xCH, xDL xDH (DL
; the line, DH the column, both from 1, as the source's). A routine's
; results are in them and in C (the 8086's CF) and Z (ZF) as the source
; gives them; A, X and Y are the routine's own unless it says.

.global xAL, xAH, xBL, xBH, xCL, xCH, xDL, xDH, xSI, xDI

; SCNDRV.ASM's state (GWRAM.ASM's DSEG names)
.global LINCNT, CRTWID, WDOTOP, WDOBOT, WDOLFT, WDORGT, TTCOL, TTTRM, TRMCUR
.global FSTLIN, FSTCOL, LSTLNE, LSTCOL, F_CRET, CSRY, CSRX, KEYSW, ESCFLG
.global F_EDIT, F_INST, CSRTYP, CSRFLG, TWOBYT, F_EDP6, MKINP

; SCNDRV.ASM's routines the other modules call
.global SCNIPL8, SCNBRK, SCNSWI, SCNCLR, SCNPOS, SCNRDT, SCNWTT, TTBINI, SCNOUT
.global SCNMRK, SCNRDL, LSTART, CSRUP, WHOME, KEYDSP

; The OEM's routines (p6/oem.s)
.global SCROUT, SCRINP, CLREOL, SCROLL, CLRSCN, SETCSR, CSRDSP, STCTYP, CBEEP
.global PUTC, P6IN, P6OUT, FLUSH, ATTR, SCATTR, VSHOW, APAGE, VPAGE, PGINIT, BANDS
.global KEYIN, KEYSIN, KDRAIN

NMLINE          = 30                   ; OEM.H's lines: the most rows (WIDTH n,30)
TRMLNF          = 10                   ; SCNDRV.ASM's terminators
TRMEOL          = $7F
TRMWRP          = 0
TRMNWP          = 1
TRMNUL          = $80
CHRBKW          = 2                    ; SCNDRV.ASM's control characters
CHRCLE          = 5
CHRFDW          = 6
CHRBAK          = 8
CHRLNF          = 10
CHRHOM          = 11
CHRERA          = 12
CHRRET          = 13
CHRAPP          = 14
CHRINS          = 18
CHRLDL          = 21
CHRADV          = 28
CHRREG          = 29
CHRUP           = 30
CHRDWN          = 31
CHRDEL          = 127
CHREDT          = 1
.global PRTMAP, EDTMAP, SCRATR, FGC, BGC, BRDC, CROW, NPAGES, STRIDE, CACHE, CURON, APGSET

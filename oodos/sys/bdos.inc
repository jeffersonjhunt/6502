; The BDOS's parts (bdos.s, bdosfile.s, bdosdisk.s) share these: block 60's
; code, its zero page and its variables in the system block. bdos.s defines
; the variables; .global makes each an export there and an import elsewhere.

.include "sys.inc"

; The FCB (CP/M 3's offsets), as the BDOS's copy F has it
FCB_DR          = 0                    ; the drive: 0 the current one, 1-16 A-P
FCB_NAME        = 1                    ; f1-f8, t1-t3; bit 7 of each an attribute
FCB_F5          = 5                    ; f5': delete XFCBs only (19)
FCB_F6          = 6                    ; f6': set the byte count (30; open gives it for CR $FF)
FCB_F7          = 7                    ; f7': opened without a write-protected file's password: read-only
FCB_F8          = 8                    ; f8': opened from user 0 (a SYS file): read-only
FCB_T1          = 9                    ; t1': read-only
FCB_T2          = 10                   ; t2': SYS
FCB_T3          = 11                   ; t3': archive
FCB_EX          = 12
FCB_S1          = 13                   ; the directory's last record byte count
FCB_S2          = 14                   ; bit 7 set: not written since opened or closed
FCB_RC          = 15
FCB_D           = 16                   ; the block pointers: 16 bytes, or 8 words
FCB_NEW         = 16                   ; rename's new name
FCB_CR          = 32
FCB_R           = 33                   ; r0-r2
FCB_SEQ         = 33                   ; bytes read and written back: sequential
FCB_RAND        = 36                   ; and random
S2_CLEAN        = $80
S2_NOENT        = $40                  ; a read found no entry for the extent: a write makes it

ENTRY_LEN       = 32
DIR_FREE        = $E5
DIR_LABEL       = $20
DIR_SFCB        = $21
SFCB_SLOT       = 10                   ; an entry's stamps in its SFCB: 1 + slot x 10
MODE_ACCESS     = $40                  ; the label's mode: access stamps (in the first)
MODE_UPDATE     = $20                  ;   update stamps
MODE_CREATE     = $10                  ;   creation stamps
MODE_PASSWORD   = $80                  ;   passwords on
XFCB_MODES      = $E0                  ; an XFCB's mode: bit 7 read, 6 write, 5 delete
PW_LEN          = 8

; Errors: X when the error mode returns them (CP/M 3's H)
ER_IO           = 1                    ; Disk I/O
ER_RODISK       = 2                    ; Read Only
ER_ROFILE       = 3                    ; File R/O
ER_SELECT       = 4                    ; Invalid Drive
ER_PASSWORD     = 7                    ; Password Error
ER_EXISTS       = 8                    ; File Exists
ER_WILD         = 9                    ; ? in Filename

NBCB            = 32                   ; buffers in BCB_BLOCK
NSLOT           = 8                    ; drives' slots: A-G, then M:
CD_LEN          = 16

; FIND's modes (SMODE)
SM_ALL          = $80                  ; every entry, labels, SFCBs, XFCBs and free ones too
SM_FREE         = $40                  ; free entries
SM_XFCB         = $20                  ; the user's XFCBs as well as its files
SM_ANYNAME      = $10                  ; '?' in the name
SM_ANYEX        = $08                  ; any extent

; Zero page
.globalzp PA, PX, PP, FUNC, EDRV, ECODE, T0, T1, T2, T3, T4, T5
.globalzp CDRV, CSLOT, CD, BSH, BLM, EXM, DSM, DRM, HWM, MODE, BIG, OFFS, DPBP
.globalzp HBLK, HBASE, ALVP, LBA, BPTR, DPTR, DIDX, HP, LIM, HV, H0, H3, NP, BLK
.globalzp RECP, GBNEW, LASTB, BIOSDRV, AGE, SMODE, FCBN, EXMASK, PEX3, MASK3, PHASH
.globalzp SZ, RR, RDMA, MON
; The system block
.global FC, SPAT, SSAVE, SSMODE, SSDRV, SSIDX, SSON, DTAB, LOGVEC, ROVEC, FCBP
.global BCB_DRV, BCB_LO, BCB_HI, BCB_DIRTY, BCB_AGEL, BCB_AGEH

; bdos.s
.global ABORT, RETA, RETAX, RETFF, RET0, DERR, FCBIN, FCBOUT, FCBSEL, FCBDRV
.global PIN, POUT, DRVBIT, ISWILD
.global B_SELDSK, B_SETTRK, B_SETSEC, B_SETDMA, B_READ, B_WRITE, B_MOVE
.global B_TIME, B_SETBNK, B_XMOVE
.global PGET, PPUT, B_CONST, B_CONIN, B_CONOUT, B_LIST, B_AUXOUT, B_AUXIN, B_AUXIST, B_AUXOST
; bdosdisk.s
.global SELECT, DSLOT, LOADCD, SAVECD, GETBUF, FLUSHD, PURGED, DIRENT, DIRPUT
.global HASHPUT, NHASH, SETPAT, PATDERIV, FIND, FINDNXT, FINDFREE, MAKEENT
.global ALVBIT, ALVSET, ALVCLR, ALVFREE, MARKE, FREEE, EPTR, STAMP, STAMPU, WBACKX
.global DIRREC, RESCAN, CHANGED, RFLUSH, RDONE, PURGEM, BCBBLK
.global SFCBAT, SFCBPUT, SFREST, NOW, PUTNOW, HWMUP, INITSTAMP
.globalzp STOFS
; bdosfile.s
.global F_RESET, F_SELECT, F_OPEN, F_CLOSE, F_SFIRST, F_SNEXT, F_DELETE
.global F_SREAD, F_SWRITE, F_MAKE, F_RENAME, F_LOGIN, F_DMA, F_ALLOC, F_SETRO
.global F_ROVEC, F_ATTRIB, F_DPB, F_USER, F_RREAD, F_RWRITE, F_SIZE, F_SETRAND
.global F_DRESET, F_RWRITEZ, F_LOCK, F_ERRMODE, F_FREE, F_FLUSH, F_FREEBLK
.global F_TRUNC, MERR, EXTUSER, RODRV, NOWILD
; bdosxtra.s
.global F_MULTI, F_SETLBL, F_GETLBL, F_STAMPS, F_WXFCB, F_DEFPW
.global PWFILE, PWALL, MKXFCB, MKROOM, XCLEAR, NEWMODE
; bdossys.s
.global F_READ, F_WRITE, F_AREAD, F_AWRITE, F_LWRITE, F_RAWIO, F_AISTAT, F_AOSTAT
.global F_STRING, F_LINE, F_STAT, F_CHAIN, F_SCB, F_SERIAL, F_RETCODE, F_CONMODE
.global F_DELIM, F_PBLOCK, F_LBLOCK, F_PARSE, OUT

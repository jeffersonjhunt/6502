; OOUI's modules' own (ui/*.s): what they share. The interface is
; include/ooui.inc's; the design hw/ooui.md's.

.ifndef UI_INC
UI_INC = 1

UI_COLS         = 80                   ; the text backend's grid
UI_ROWS         = 30
UI_DEPTH        = 16                   ; the deepest a record's parents may go
UI_PAGES        = 64                   ; OOUI's card memory: one card bank, in card pages
UI_OFS_OVER     = $14                  ; its parts, in card pages from its first: the overlay's map,
UI_OFS_SPR      = $28                  ;   the sprite images (the pointer's first),
UI_OFS_FONT     = $30                  ;   the font
UI_CHAR_SHADOW  = 0                    ; (not a character: the shadow takes what is under it)

; zpuicore.s's: scratch, nothing kept between calls. UIT holds the theme
; from ENTER on; UIR a record (REC); UIP and UIQ the drawing's and the
; strings'.
.globalzp UIP, UIQ, UIR, UIT

; uicore.s's
.global ublk, uarea, utab, ucount, utheme, ukeys, ubase, uover, usprite, usprites, uoflags
.global upage, ufocus, ufwin, umenu, umsel, upress, ususp, umodal, udmg, ucli, rhid, rover, rtype
.global ENTER, REC, RECT, DLEN, DIRTY, FWIN, INSIDE, CANFOCUS, SETFOCUS, NEXTFOCUS, FINDMODAL, BOXADD, BOXFULL
.global ALLDIRTY, POINTER, LBOX, dmp, uptr, rcur, ACCEL, UPPER

; uidraw.s's
.global PAINT, SETUP, GLYPHS, EXTRECT, er, SETPAL, PTRIMG
.global GOTO, EMIT, REPT, PUTS, FILLR, BOXD, SHADOW, TATTR, NOACC, uattr, uacc, cmap, mapB, ucol, dh, TEXTLINE, tal, CLIPALL

; uimenu.s's
.global MRECT, DMBAR, DMENU, MOPEN, MCLOSE, MKEY, MCLICK, MBARAT, MALT, KEYTEXT, MCHOOSE, mcnt

; uipoll.s's
.global UCHAR, ku, km, kc, mcol, mrowp, evv, evh, evy, evf

; HID usages
K_ENTER         = $28
K_ESC           = $29
K_TAB           = $2B
K_SPACE         = $2C
K_F10           = $43
K_RIGHT         = $4F
K_LEFT          = $50
K_DOWN          = $51
K_UP            = $52

.endif

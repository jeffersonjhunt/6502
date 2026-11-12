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
.global utot, udlgp, ufl, wtype, wo, rclip, rcv, rcp, WFLAGS, WCALL, CLAMP, PUBH, CHKH, DAMAGE, REFOCUS, uopen, uwid
.global t0, t1, t2, t3

; uidraw.s's
.global upmode, pdmg, THUMBSOFF
.global PAINT, SETUP, GLYPHS, EXTRECT, er, SETPAL, PTRIMG, PTRATTR, LAYERS, PTRSET, FRAMES, upost, MAPW1
.global GOTO, EMIT, REPT, PUTS, FILLR, BOXD, SHADOW, TATTR, NOACC, uattr, uacc, cmap, mapB, ucol, dh, TEXTLINE, tal, CLIPALL

; uimenu.s's
.global MRECT, DMBAR, DMENU, MOPEN, MCLOSE, MKEY, MCLICK, MBARAT, MALT, KEYTEXT, MCHOOSE, mcnt

; uipoll.s's
.global UCHAR, ku, km, kc, mcol, mrowp, evv, evh, evy, evf
.global mpx, mpy, upend, upendh, upendv, HIT, SCOPE, PRESS, sc, wst, evk, evx, evyy, POLLINIT, FRAME
.global udf1, udf2, uidle, HIDEH, DROP, POLLSTOP, FRONT

; A widget module's record (U2): what the core calls it for. Each handler
; is entered with UIR on the widget's record and dh its handle (and, for
; the drawing, UI_RECT its place, UIT the theme); 0 for none.
WR_TYPE         = 0                    ; its UT_*
WR_FLAGS        = 1                    ; WF_*
WR_DRAW         = 2                    ; draw it at UI_RECT
WR_KEY          = 4                    ; a key (ku, km, kc) while it has the focus: carry clear and A an
                                       ;   event (UE_NONE: taken), or carry set: not taken
WR_DOWN         = 6                    ; the left button down on it (mcol, mrowp; mpx, mpy from its top
                                       ;   left in pointer units): A an event; it is held (upress) till up
WR_MOVE         = 8                    ; the pointer moved while held: A an event
WR_UP           = 10                   ; the left button up: A an event
WR_WHEEL        = 12                   ; A the wheel's steps over it: as WR_KEY
WR_LEN          = 14
WF_FOCUS        = $01                  ; it takes the focus
WF_RANGE        = $02                  ; its value is held to 0-UW_A
WF_LIST         = $04                  ;   below UW_A, or $FFFF none
WF_REPEAT       = $08                  ; the keys it takes repeat while held

DLG_RECS        = 14                   ; (U3b: 14, the picker's Add all)

; The crossings between blocks (U3a: uifar.s, and each block's dispatcher,
; uia.s, uib.s, uic.s): XGO FARx, a selector, keeping A, X, Y, the carry
; and V (N and Z not: review C5).
.global xsel, xsx, xjmp, FARA, FARB, FARC, AENT, BENT, CENT
XA_HIDEH        = 0                    ; uia.s's ATAB
XA_REFOCUS      = 2
XB_INIT         = 0                    ; uib.s's BTAB
XB_WCALL        = 2
XB_POST         = 4
XC_DF1          = 0                    ; uic.s's CTAB
XC_DF2          = 2
XC_IDLE         = 4
XC_ROWS         = 6
PR_ROWS         = 16                   ; uirows.s's prbuf: the picker's rows shown
PR_LEN          = 40                   ;   each one's text with its 0, as uipick.s's rowbuf
.global prtop, prn, prsty, prbuf, PROFS, PROWS
.macro XGO far, sel
        pha
        lda     #sel
        sta     xsel
        pla
        jsr     far
.endmacro                   ; the dialogs' records (uidlg.s): the picker's 9, the box's 4

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
K_BACK          = $2A
K_PGUP          = $4B
K_PGDN          = $4E
K_HOME          = $4A
K_END           = $4D
K_DEL           = $4C

.endif

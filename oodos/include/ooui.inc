; OOUI, the OO-1's UI toolkit (hw/ooui.md, "The API"; proposed): windows,
; menus, buttons and the rest, drawn on the video card's text layers. From
; U3a (proposed) OOUI is a banked library, OOUI.BNK, in three blocks the
; program allocates and reaches by FARCALL (hw/ooui.md, "U3a: OOUI as a
; banked library"): the program links build/ooui.lib, the program part (the
; calls' stubs, ui_load, UISTATE, OOUI's zero page, and the themes UI_HOUSE
; and UI_TURBO when it names them), then build/oolib.lib, and puts the
; UISTATE segment in a memory area of its own on a page outside $4000-$7FFF
; (test/ui/uiprog.cfg shows how). Include this after oodos.inc and oolib.inc.
;
; ui_load      A/X an FCB (its drive and name set), or 0 for A:OOUI.BNK in
;              user 0: OOUI.BNK loaded and the stubs filled. After GX_INIT,
;              before ui_open. Refusals: UX_STATE, UX_BUSY, UX_LOAD,
;              UX_VERSION, UX_MEMORY, UX_GX (uiprog/uiload.s says when).
;
; Phase U1 built the core and the text backend: the desktop, windows,
; panels, labels, buttons, check boxes, the menu bar and its menus, focus,
; the themes, OOUI's glyphs, overlays on layer 1 and the pointer. Phase U2
; (proposed) adds the other widgets, the message box (ui_msgbox) and the
; file picker (ui_pick). Every widget is in OOUI.BNK (U3a), so UO_WIDGETS is
; ignored and UI_END's name_WIDGETS is 0.
;
; Before ui_open the program opens an event area (function 80's FT_OPEN,
; layout 2) and calls GX_INIT with it, then GX_PALINIT and GX_SPRINIT.
;
; The calls (every one changes A, X and Y, W1 and aperture slot 1; carry
; clear for done, set and A an error UX_* for a refusal; D clear on return):
;
;   ui_open     A/X a block of UO_LEN (below): the table checked, OOUI's
;               card memory taken, its font, layers, colours and pointer set,
;               everything marked to be drawn, the focus on the first widget
;               that takes it. Nothing drawn until ui_poll
;   ui_close    the card memory freed, the pointer off, the console back
;   ui_poll     one event: A its code (UE_*), X its widget, Y as the event
;               says, and UI_EV. Before it returns UE_NONE it draws what
;               changed
;   ui_show     A a handle: shown, with its children
;   ui_hide     A a handle: hidden, with its children; what it covered drawn
;               again
;   ui_flags    Y a handle, A the flags to set, X those to clear (not
;               UF_DIRTY or UF_HIDDEN): A the flags now; (U3b) a slider's
;               or a scroll bar's checked as ui_open checks them, carry set
;               and A UX_SPRITE if refused, the flags unchanged
;   ui_settext  Y a handle, A/X the text
;   ui_setvalue Y a handle, A/X the value (a check box's or a toggle's 0 or 1)
;   ui_getvalue Y a handle: A/X its value
;   ui_dirty    A a handle: drawn again, with its children, at ui_poll
;   ui_redraw   everything drawn again
;   ui_focus    A a handle: the focus there; carry set if it can't take it
;   ui_getfocus A the focus, UI_NONE none
;   ui_rect     A a handle: UI_RECT its column, row, width and height on the
;               screen, in cells; carry set if it is hidden
;   ui_info     A the grid's columns, X its rows, Y the backend (UB_TEXT)
;   ui_theme    A/X a theme (0: the default): everything drawn again
;   ui_suspend  OOUI stops drawing, and ui_poll gives every event as UE_RAW
;   ui_resume   the layers, colours and pointer set again; everything drawn
;   ui_setrange Y a handle, A/X a list's count, or a bar's, slider's, scroll
;               bar's or choice's top: the value held to it (U2)
;   ui_msgbox   A/X a block of UM_LEN: the message box up (U2, uimsg.s)
;   ui_pick     A/X a block of UP_LEN: the file picker up (U2, uipick.s)
;   ui_relist   the picker's directory read again (U2)

.ifndef OOUI_INC
OOUI_INC = 1

.global ui_open, ui_close, ui_poll, ui_show, ui_hide, ui_flags, ui_settext, ui_setvalue, ui_getvalue
.global ui_dirty, ui_redraw, ui_focus, ui_getfocus, ui_rect, ui_info, ui_theme, ui_suspend, ui_resume
.global ui_setrange, ui_msgbox, ui_pick, ui_relist
.global UI_EV, UI_RECT, UI_RAW
.global ui_load, UISTATE
.global UI_HOUSE, UI_TURBO              ; the themes: the default (OO-1's house look), Turbo Vision's blue

; ---------------------------------------------------------------------------
; Handles: a record's index in the table.
UI_MAXREC       = 240                  ; handles 0-239
UI_MSG          = $F0                  ; the message box (U2)
UI_PICKER       = $F1                  ; the file picker (U2)
UI_NONE         = $FF                  ; no widget

; A widget record: UW_LEN bytes.
UW_TYPE         = 0                    ; UT_*
UW_PARENT       = 1                    ; the parent's handle, UI_NONE at the top
UW_X            = 2                    ; column, signed, in the parent's client area: negative from its right
UW_Y            = 3                    ; row, the same from its bottom
UW_W            = 4                    ; width in cells; 0 to the client's edge, negative that many short of it
UW_H            = 5                    ; height, the same
UW_FLAGS        = 6                    ; UF_*
UW_STATE        = 7                    ; OOUI's (US_*): the program leaves it 0
UW_TEXT         = 8                    ; a 0-terminated string, 0 none
UW_VALUE        = 10                   ; by type
UW_A            = 12
UW_B            = 14
UW_LEN          = 16

; The types (provisional numbers; hw/ooui.md). U1's: 0-6 and 9-12.
UT_NONE         = 0                    ; an unused record
UT_DESKTOP      = 1
UT_WINDOW       = 2                    ; text: the title
UT_PANEL        = 3                    ; text: the title (0 none)
UT_LABEL        = 4
UT_BUTTON       = 5                    ; value: a toggle's state, 0 or 1
UT_CHECK        = 6                    ; value: 0 or 1
UT_GROUP        = 7                    ; U2
UT_RADIO        = 8                    ; U2
UT_MENUBAR      = 9
UT_MENU         = 10                   ; text: the title; a menu bar's child
UT_ITEM         = 11                   ; a menu's child: value a check mark (0, 1), +12 a key's usage, +13 its UK_*
UT_SEP          = 12                   ; a menu's line
UT_VSCROLL      = 13                   ; U2, from here
UT_HSCROLL      = 14
UT_LIST         = 15
UT_FIELD        = 16
UT_VBAR         = 17
UT_HBAR         = 18
UT_VSLIDER      = 19
UT_HSLIDER      = 20
UT_CANVAS       = 21
UT_CHOICE       = 22                   ; U2 (proposed): text an array of UW_A strings, value the one shown
UT_COUNT        = 23

; UW_FLAGS: bits 0-3 every type's, 4-7 the type's own.
UF_HIDDEN       = $01
UF_DISABLED     = $02
UF_DIRTY        = $04                  ; OOUI's
UF_NOFOCUS      = $08
UF_CLOSE        = $10                  ; a window: its close box
UF_MODAL        = $20                  ;   it takes the input while it shows
UF_OVERLAY      = $40                  ;   on the overlay layer
UF_NOFRAME      = $80                  ;   no frame and no title
UF_FRAME        = $10                  ; a panel: framed
UF_CENTRE       = $10                  ; a label: centred
UF_RIGHT        = $20                  ;   right-aligned
UF_DEFAULT      = $10                  ; a button: Enter presses it
UF_CANCEL       = $20                  ;   Escape presses it
UF_TOGGLE       = $40                  ;   a toggle: each press flips its value
UF_MULTI        = $10                  ; a list: rows marked (UE_MARK)
UF_SCROLL       = $20                  ;   its own scroll bar, in its last column
UF_ROUTINE      = $40                  ;   its rows from a routine (UW_TEXT); (U3b) a canvas: its cells too
UF_UPPER        = $10                  ; a field: letters in capitals
UF_SPRITE       = $10                  ; a slider: its thumb a sprite (+15); (U3b) a scroll bar: the same (+9)
UF_OWNTHUMB     = $20                  ; (U3b) with UF_SPRITE: the program's own thumb image, which OOUI
                                       ;   never writes; its size byte (GX_SPRATTR's Y: height << 6,
                                       ;   width << 4, the palette group; not 0) at UW_TEXT (+8)

; UW_STATE (OOUI's)
US_PRESSED      = $01
US_HOT          = $02
US_OPEN         = $04                  ; a menu down
US_FOCUS        = $08
US_DRAG         = $10
US_EDIT         = $20                  ; a field edited since it took the focus (U2)

; ---------------------------------------------------------------------------
; ui_open's block
UO_AREA         = 0                    ; the event area (layout 2; 256 bytes advised)
UO_TABLE        = 2                    ; the widget table
UO_COUNT        = 4                    ; its records, 1-240
UO_THEME        = 5                    ; a theme, 0 the default
UO_KEYS         = 7                    ; the accelerator table, 0 none
UO_BASE         = 9                    ; the widgets' layer, 0 or 1
UO_OVER         = 10                   ; the menus' and overlay windows' layer, 0 or 1, or $FF: the base layer
UO_SPRITE       = 11                   ; the first sprite OOUI may use (the pointer)
UO_SPRITES      = 12                   ; how many, 0-16 (0: no pointer)
UO_FLAGS        = 13                   ; UO_OWNMOUSE
UO_WIDGETS      = 14                   ; ignored (U3a: every widget is in OOUI.BNK); UI_END's name_WIDGETS is 0
UO_LEN          = 16
UO_OWNMOUSE     = $01                  ; the program sets the mouse's mode and the pointer itself

; A list's row routine's Y (U2)
UL_NORMAL       = 0
UL_CURRENT      = 1
UL_DIM          = 2
UL_MARKED       = $80

; A canvas's UV_KIND (U2)
UC_PRESS        = 1
UC_RELEASE      = 2
UC_MOVE         = 3
UC_WHEEL        = 4

; ui_msgbox's block (U2)
UM_TEXT         = 0                    ; the text, lines split at CR ($0D), at most 4
UM_TITLE        = 2
UM_BUTTONS      = 4                    ; UM_OK, UM_OKCANCEL, UM_YESNO
UM_DEFAULT      = 5                    ; the button the focus starts on, 0 or 1
UM_LEN          = 6
UM_OK           = 0
UM_OKCANCEL     = 1
UM_YESNO        = 2

; ui_pick's block (U2)
UP_TITLE        = 0
UP_TYPES        = 2                    ; three-letter types one after another, ended by 0; 0 every file
UP_FLAGS        = 4                    ; UP_MULTI, UP_NEW, UP_DRIVES, UP_USERS; (U3b) UP_KEEP, UP_ALL
UP_DRIVE        = 5                    ; the drive to start on (0 the current, 1 A:)
UP_USER         = 6                    ; the user to start on ($FF the current)
UP_RESULT       = 7                    ; the result buffer: UP_ELEN bytes an entry
UP_ROOM         = 9                    ; its room, in entries
UP_CHOSEN       = 10                   ; the entries chosen (OOUI writes it)
UP_COLUMN       = 11                   ; a column routine, 0 none
UP_EXTRA        = 13                   ; an extra button's text, 0 none
UP_WORK         = 15                   ; a work buffer for the listing
UP_WORKLEN      = 17                   ;   its length: UP_ELEN bytes a file listed
UP_LEN          = 20
UP_MULTI        = $01
UP_NEW          = $02
UP_DRIVES       = $04
UP_USERS        = $08
UP_KEEP         = $10                  ; (U3b) OK is "Add": UE_PICK 0 gives the chosen and the picker stays up
UP_ALL          = $20                  ; (U3b) an "Add all" button: UE_PICK 3, every file listed given, still up
UP_EDRIVE       = 0                    ; an entry: the drive (1 A:)
UP_EUSER        = 1                    ;   the user
UP_ENAME        = 2                    ;   the name and type, 8 + 3, as an FCB holds them
UP_ELEN         = 16

; ui_info's Y
UB_TEXT         = 0
UB_BITMAP       = 1

; The errors (A, carry set)
UX_VERSION      = 1                    ; OODOS's interface below 8, or no input or DMA
UX_AREA         = 2                    ; no event area, or not layout 2
UX_MEMORY       = 3                    ; no card memory
UX_TABLE        = 4                    ; an unknown type, a parent after its child, a menu's or an item's parent
UX_HANDLE       = 5                    ; a handle past the table, or a call the type doesn't take
UX_BUSY         = 6                    ; ui_open while OOUI is open
UX_LAYER        = 7                    ; ui_open: UO_BASE past 1, or UO_OVER not $FF and not above the base
UX_PICK         = 8                    ; ui_pick: a block it can't take (no work buffer, no room) (U2)
UX_SPRITE       = 9                    ; ui_open: a slider's thumb sprite not one of OOUI's after the pointer's (U2)
UX_LOAD         = 10                   ; ui_load (U3a): no file, a short read, not an OOUI.BNK
UX_GX           = 11                   ; ui_load: OOLIB refused a block in W1 (GX_INIT not called, or refused)
UX_STATE        = 12                   ; ui_load: UISTATE not on a page

UI_IFACE        = 1                    ; OOUI.BNK's interface version this program is built for (U3a)
UI_STATE_LEN    = 1536                 ; UISTATE's bytes: whole pages (hw/ooui.md, U3a: answer 5, question 8)
UI_STUB_LEN     = 7                    ; a call's stub (uiprog/uistub.s)

; ---------------------------------------------------------------------------
; Events (ui_poll's A, and UI_EV's UV_CODE)
UE_NONE         = 0                    ; nothing waits: what changed is drawn
UE_CLICK        = 1                    ; X a button: pressed; UV_VALUE a toggle's new state
UE_CHANGE       = 2                    ; X a check box (U1): UV_VALUE its new value
UE_SELECT       = 3
UE_MARK         = 4
UE_MENU         = 5                    ; X the item, Y its menu
UE_CLOSE        = 6                    ; X the window: its close box, or Escape
UE_FOCUS        = 7                    ; X the widget the focus moved to
UE_KEY          = 8                    ; X the focus, Y the usage: a key nothing took; UV_MODS, UV_CHAR
UE_WHEEL        = 9                    ; X the widget under the pointer: UV_VALUE the steps, signed
UE_CANVAS       = 10
UE_PICK         = 11
UE_RAW          = 12                   ; Y the kind (EK_*): an event OOUI doesn't use, in UI_RAW
UE_MODAL        = 13

; UI_EV
UV_CODE         = 0
UV_HANDLE       = 1
UV_VALUE        = 2                    ; 2 bytes
UV_X            = 4                    ; 2 bytes: the pointer, in pointer units from the widget's top left
UV_Y            = 6
UV_KIND         = 7
UV_MODS         = 8                    ; EV_MODS's then
UV_USAGE        = 9
UV_CHAR         = 10                   ; ASCII, 0 none
UV_FLAGS        = 11
UV_LEN          = 12
UV_LIVE         = $01
UV_DOUBLE       = $02
UV_KEYED        = $04                  ; by the keyboard

; The accelerator table (UO_KEYS): UK_LEN bytes an entry, ended by a usage of 0.
UK_USAGE        = 0                    ; the key's HID usage
UK_MODS         = 1                    ; the modifiers that must be held (UK_CTRL ...), 0 none
UK_HANDLE       = 2
UK_ACTION       = 3                    ; UA_*
UK_LEN          = 4
UK_CTRL         = $11                  ; either side's bit of EV_MODS
UK_SHIFT        = $22
UK_ALT          = $44
UA_PRESS        = 0                    ; as if clicked: a button, a check box, a menu's item
UA_UP           = 1                    ; U2's (sliders, scroll bars, lists)
UA_DOWN         = 2
UA_FOCUS        = 3

; ---------------------------------------------------------------------------
; A theme: TH_LEN bytes.
TH_VERSION      = 0                    ; 1
TH_FLAGS        = 1                    ; TF_*
TH_PALETTE      = 2                    ; 16 colours (GX_PALSET's two bytes each), 0 the CGA set
TH_OVERPAL      = 4                    ; the overlay's 16 colours, in TH_OVERGROUP (0: TH_PALETTE's)
TH_GROUP        = 6                    ; the base layer's palette group (PALOFS)
TH_OVERGROUP    = 7                    ; the overlay layer's, and the pointer's
TH_DIMGROUP     = 8                    ; the base's under a modal window on the overlay (TF_DIM)
TH_DESKCHAR     = 9                    ; the desktop's character
TH_METRICS      = 10                   ; in cells: frame, title rows, button padding, scroll bar, shadow columns,
TM_FRAME        = 10                   ;   shadow rows, between menu titles, reserved
TM_TITLE        = 11
TM_PAD          = 12
TM_SCROLL       = 13
TM_SHADOWX      = 14
TM_SHADOWY      = 15
TM_MENUGAP      = 16
TH_PIXELS       = 18                   ; the bitmap backend's
TH_TIMES        = 26                   ; in frames: repeat delay, repeat, double-click, cursor
TH_FRAMES       = 30                   ; TS_*: a window's, a dialog's, a panel's, a menu's
TH_COLOURS      = 34                   ; an attribute (background << 4 | foreground) for each TC_* role
TH_POINTER      = 63                   ; the pointer's colours: its outline << 4 | its fill
TH_LEN          = 64
TF_SHADOW       = $01
TF_DIM          = $02
TF_HOT          = $04                  ; (U3b) a button under the pointer (US_HOT) in TC_BUTTONFOCUS's colours
TS_NONE         = 0
TS_SINGLE       = 1
TS_DOUBLE       = 2
TS_HEAVY        = 3

; The colour roles: TH_COLOURS + n
TC_DESKTOP      = TH_COLOURS + 0
TC_FRAME        = TH_COLOURS + 1
TC_TITLE        = TH_COLOURS + 2
TC_TITLEOFF     = TH_COLOURS + 3
TC_CLIENT       = TH_COLOURS + 4
TC_LABEL        = TH_COLOURS + 5
TC_BUTTON       = TH_COLOURS + 6
TC_BUTTONFOCUS  = TH_COLOURS + 7
TC_BUTTONDOWN   = TH_COLOURS + 8
TC_DISABLED     = TH_COLOURS + 9
TC_ACCEL        = TH_COLOURS + 10      ; its foreground, on the widget's background
TC_CHECK        = TH_COLOURS + 11
TC_MENUBAR      = TH_COLOURS + 12
TC_MENU         = TH_COLOURS + 13
TC_MENUSEL      = TH_COLOURS + 14
TC_LIST         = TH_COLOURS + 15
TC_LISTSEL      = TH_COLOURS + 16
TC_LISTCUR      = TH_COLOURS + 17
TC_LISTDIM      = TH_COLOURS + 18
TC_LISTMARK     = TH_COLOURS + 19
TC_FIELD        = TH_COLOURS + 20
TC_FIELDFOCUS   = TH_COLOURS + 21
TC_SCROLL       = TH_COLOURS + 22
TC_THUMB        = TH_COLOURS + 23
TC_BAR          = TH_COLOURS + 24
TC_BAREMPTY     = TH_COLOURS + 25
TC_SHADOW       = TH_COLOURS + 26
TC_DIALOG       = TH_COLOURS + 27
TC_DIALOGFRAME  = TH_COLOURS + 28
TC_COUNT        = 29

; OOUI's glyphs (the text backend's font: the console's, with these redefined)
UG_VBAR         = $00                  ; + n (1-15): a vertical bar n/16 full from the bottom
UG_HBAR1        = $12                  ; $12-$17: a horizontal bar 1/8-6/8 full from the left
UG_HBAR7        = $1C                  ;   and 7/8
UG_BOX          = $7F                  ; a check box empty
UG_BOXON        = $9B                  ;   ticked
UG_RADIO        = $9C                  ; a radio button off
UG_RADIOON      = $9D                  ;   on
UG_FOLDER       = $9E
UG_DRIVE        = $9F

; ---------------------------------------------------------------------------
; Static layouts: one record a macro, `id` its handle, between UI_BEGIN and
; UI_END, which put the table in DATA (OOUI writes its records' flags and
; state). A text is a string literal (put in RODATA with its 0) or an
; address (0 none).

.macro _UI_TEXT text
        .if .match({text}, "")
                .local s
                .pushseg
                .rodata
s:              .asciiz text
                .popseg
                .word   s
        .else
                .word   text
        .endif
.endmacro

.macro _UI_REC id, type, parent, xx, yy, ww, hh, flags, text, value, aa, bb
        id = _UI_N
        .assert (parent) = UI_NONE || (parent) < _UI_N, error, "OOUI: a parent comes before its child"
        .assert _UI_N < UI_MAXREC, error, "OOUI: more than 240 records"
        .byte   type, parent, <(xx), <(yy), <(ww), <(hh), flags, 0
        _UI_TEXT text
        .word   value, aa, bb
        _UI_N .set _UI_N + 1
.endmacro

.macro UI_BEGIN name
        _UI_N .set 0
        .pushseg
        .data
name:
.endmacro
.macro UI_END name
        .ident(.concat(.string(name), "_COUNT")) = _UI_N
        .popseg
        .pushseg
        .rodata
.ident(.concat(.string(name), "_WIDGETS")) = 0   ; UO_WIDGETS: ignored (U3a)
        .popseg
.endmacro
.macro UI_DESKTOP id
        _UI_REC id, UT_DESKTOP, UI_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0
.endmacro
.macro UI_WINDOW id, parent, xx, yy, ww, hh, text, flags
        _UI_REC id, UT_WINDOW, parent, xx, yy, ww, hh, flags, text, 0, 0, 0
.endmacro
.macro UI_PANEL id, parent, xx, yy, ww, hh, text, flags
        _UI_REC id, UT_PANEL, parent, xx, yy, ww, hh, flags, text, 0, 0, 0
.endmacro
.macro UI_LABEL id, parent, xx, yy, ww, text, flags, attr
.ifblank attr                           ; (U3b) attr: the label's own attribute, 0 the theme's TC_LABEL
        _UI_REC id, UT_LABEL, parent, xx, yy, ww, 1, flags, text, 0, 0, 0
.else
        _UI_REC id, UT_LABEL, parent, xx, yy, ww, 1, flags, text, attr, 0, 0
.endif
.endmacro
.macro UI_BUTTON id, parent, xx, yy, ww, text, flags
        _UI_REC id, UT_BUTTON, parent, xx, yy, ww, 1, flags, text, 0, 0, 0
.endmacro
.macro UI_CHECK id, parent, xx, yy, ww, text, value, flags
        _UI_REC id, UT_CHECK, parent, xx, yy, ww, 1, flags, text, value, 0, 0
.endmacro
.macro UI_MENUBAR id, parent
        _UI_REC id, UT_MENUBAR, parent, 0, 0, 0, 1, 0, 0, 0, 0, 0
.endmacro
.macro UI_MENU id, menubar, text
        _UI_REC id, UT_MENU, menubar, 0, 0, 0, 0, 0, text, 0, 0, 0
.endmacro
.macro UI_ITEM id, menu, text, usage, mods, flags
        _UI_REC id, UT_ITEM, menu, 0, 0, 0, 1, flags, text, 0, (usage) | (mods) << 8, 0
.endmacro
.macro UI_SEP id, menu
        _UI_REC id, UT_SEP, menu, 0, 0, 0, 1, 0, 0, 0, 0, 0
.endmacro
; U2's (proposed): each marks its module for UI_END's list.
.macro UI_GROUP id, parent, xx, yy, ww, hh, text, value, flags
        _UI_U_GROUP .set 1
        _UI_REC id, UT_GROUP, parent, xx, yy, ww, hh, flags, text, value, 0, 0
.endmacro
.macro UI_RADIO id, group, xx, yy, ww, text, flags
        _UI_U_RADIO .set 1
        _UI_REC id, UT_RADIO, group, xx, yy, ww, 1, flags, text, 0, 0, 0
.endmacro
.macro UI_VSCROLL id, parent, xx, yy, len, last, page, flags, sprite, thumb
        _UI_U_VSCROLL .set 1
        _UI_THUMB sprite, thumb
        _UI_REC id, UT_VSCROLL, parent, xx, yy, 1, len, flags, _UI_T, 0, last, page
.endmacro
.macro UI_HSCROLL id, parent, xx, yy, len, last, page, flags, sprite, thumb
        _UI_U_HSCROLL .set 1
        _UI_THUMB sprite, thumb
        _UI_REC id, UT_HSCROLL, parent, xx, yy, len, 1, flags, _UI_T, 0, last, page
.endmacro
; (U3b) _UI_T: a scroll bar's UW_TEXT, its thumb's sprite (high byte) and the size byte of the
; program's own thumb (low byte); a slider's UW_TEXT, that size byte. Each 0 when not given.
.macro _UI_THUMB sprite, thumb
        _UI_T .set 0
.ifnblank sprite
        _UI_T .set (sprite) << 8
.endif
.ifnblank thumb
        _UI_T .set _UI_T | (thumb)
.endif
.endmacro
.macro UI_LIST id, parent, xx, yy, ww, hh, rows, count, flags
        _UI_U_LIST .set 1
        _UI_REC id, UT_LIST, parent, xx, yy, ww, hh, flags, rows, $FFFF, count, 0
.endmacro
.macro UI_FIELD id, parent, xx, yy, ww, buffer, size, flags
        _UI_U_FIELD .set 1
        _UI_REC id, UT_FIELD, parent, xx, yy, ww, 1, flags, buffer, 0, size, 0
.endmacro
.macro UI_VBAR id, parent, xx, yy, len, top, flags
        _UI_U_VBAR .set 1
        _UI_REC id, UT_VBAR, parent, xx, yy, 1, len, flags, 0, 0, top, 0
.endmacro
.macro UI_HBAR id, parent, xx, yy, len, top, flags
        _UI_U_HBAR .set 1
        _UI_REC id, UT_HBAR, parent, xx, yy, len, 1, flags, 0, 0, top, 0
.endmacro
.macro UI_VSLIDER id, parent, xx, yy, len, top, value, step, sprite, flags, thumb
        _UI_U_VSLIDER .set 1
        _UI_THUMB , thumb
        _UI_REC id, UT_VSLIDER, parent, xx, yy, 1, len, flags, _UI_T, value, top, (step) | (sprite) << 8
.endmacro
.macro UI_HSLIDER id, parent, xx, yy, len, top, value, step, sprite, flags, thumb
        _UI_U_HSLIDER .set 1
        _UI_THUMB , thumb
        _UI_REC id, UT_HSLIDER, parent, xx, yy, len, 1, flags, _UI_T, value, top, (step) | (sprite) << 8
.endmacro
.macro UI_CANVAS id, parent, xx, yy, ww, hh, flags, routine
        _UI_U_CANVAS .set 1
.ifblank routine                        ; (U3b) routine: with UF_ROUTINE, the row routine of its cells
        _UI_REC id, UT_CANVAS, parent, xx, yy, ww, hh, flags, 0, 0, 0, 0
.else
        _UI_REC id, UT_CANVAS, parent, xx, yy, ww, hh, flags, routine, 0, 0, 0
.endif
.endmacro
.macro UI_CHOICE id, parent, xx, yy, ww, texts, count, value, flags
        _UI_U_CHOICE .set 1
        _UI_REC id, UT_CHOICE, parent, xx, yy, ww, 1, flags, texts, value, count, 0
.endmacro

.endif

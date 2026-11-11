; OOUI, the OO-1's UI toolkit (hw/ooui.md, "The API"; proposed): windows,
; menus, buttons and the rest, drawn on the video card's text layers by a
; library linked into the program. Link a program with build/ooui.lib, then
; build/oolib.lib (OOUI draws through OOLIB), and include this after
; oodos.inc and oolib.inc.
;
; Phase U1 builds the core and the text backend: the desktop, windows,
; panels, labels, buttons, check boxes, the menu bar and its menus, focus,
; the themes, OOUI's glyphs, overlays on layer 1 and the pointer. The other
; types are U2's: ui_open refuses them (UX_TABLE) until then.
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
;               UF_DIRTY or UF_HIDDEN): A the flags now
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

.ifndef OOUI_INC
OOUI_INC = 1

.global ui_open, ui_close, ui_poll, ui_show, ui_hide, ui_flags, ui_settext, ui_setvalue, ui_getvalue
.global ui_dirty, ui_redraw, ui_focus, ui_getfocus, ui_rect, ui_info, ui_theme, ui_suspend, ui_resume
.global UI_EV, UI_RECT, UI_RAW
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

; UW_STATE (OOUI's)
US_PRESSED      = $01
US_HOT          = $02
US_OPEN         = $04                  ; a menu down
US_FOCUS        = $08
US_DRAG         = $10

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
UO_LEN          = 16
UO_OWNMOUSE     = $01                  ; the program sets the mouse's mode and the pointer itself

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
TH_OVERPAL      = 4                    ; the overlay's (0: the base's). U1: group 0 only (hw/ooui.md, question 7)
TH_GROUP        = 6
TH_OVERGROUP    = 7
TH_DIMGROUP     = 8
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
.macro UI_LABEL id, parent, xx, yy, ww, text, flags
        _UI_REC id, UT_LABEL, parent, xx, yy, ww, 1, flags, text, 0, 0, 0
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

.endif

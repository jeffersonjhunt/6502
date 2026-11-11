# OOUI: the OO-1's UI toolkit #

Nov 10, 2026 · @Jefferson

**Status: proposed (2026-11-10).** Phase U0 (below) settles the API and the open questions, as OOLIB's plan did.
**U0's proposal (2026-11-10), owner to confirm:** [the API](#the-api), [what OO-Amp needs](#what-oo-amp-needs-from-ooui)
and a recommended answer to each [open question](#open-questions).
The video memory it allocates from is changing with [video-rev2.md](video-rev2.md); OOUI's API doesn't depend on
which card is fitted. A richer, LVGL-based GPU was considered and parked for a future machine:
[future/gpu-lvgl.md](future/gpu-lvgl.md).


## Goal and approach ##

OOUI is a 6502 library that gives every OO-1 app the same windows, menus, dialogs, lists, scroll bars and file
picker, drawn on the video card's layers. Apps stop hand-building UI, and the widget code is written once,
centrally.

- **No new display hardware.** It runs at 640×480 on the palette-based layers. An optional drawing assist is the
  only hardware change considered (below).
- **API first.** The calls describe what an app wants (a window, a list, a dialog), not how it's drawn, so a
  richer backend can replace the drawing later without changing apps.
- **Text layer first,** then a bitmap backend if the look needs it.
- **The proving app is OO-Amp.** If porting it makes it clearly smaller and simpler, the toolkit works.


## Decisions ##

| Topic | Decision |
| --- | --- |
| Library | OOUI, separate from OOLIB, prefix `ui`. Built as an ar65 archive like `oolib.lib`, so ld65 links only the modules an app uses |
| Dependency | OOUI uses OOLIB (event area, `gx` drawing, assets, video memory allocation); OOLIB never depends on OOUI |
| Where it runs | Entirely on the 6502, linked into the app. No resident OODOS service is needed at first |
| Display | 640×480, existing layer types, palette-based. No new modes |
| Backends | Text layer first (80×30); bitmap later if needed |
| Video memory | Allocated through OOLIB, so it works on today's card and on revision 2 ([video-rev2.md](video-rev2.md)) |
| Apple II mode | Unaffected: OOUI runs only under OODOS in native mode |
| Docs | This design record, and its own Programmer's Guide chapter |


## Architecture ##

```
  Apps (OO-Amp first)        widget tables, ui_open, ui_poll; apps never draw
        │
  OOUI                       widgets, focus, menus, dialogs, file picker, themes
        │   ┌─ text backend (first): 80x30 on layers 0 and 1, CP437 glyphs
        │   └─ bitmap backend (later): same API, theme metrics in pixels
        ▼
  OOLIB                      event area (keys, mouse), gx drawing, DMA copies, assets
  OODOS                      directory calls for the file picker, pointer sprite, frame handler
        │
  Hardware                   video card layers, sprites, palette; USB keyboard and mouse; DMA
```

Apps only call OOUI. OOUI reads input from OOLIB's event area, draws through a backend, and uses OODOS for
directories and the pointer.


## Widgets and API shape ##

The app describes widgets in a table in its own memory; OOUI draws them, routes input and reports events.

**Widget set, in the order they're built**

1. Desktop, window (title, frame, close box), panel and label.
2. Button, checkbox, radio group, menu bar with drop-down menus.
3. Scroll bar, list box (scrolling, selection), message box and confirm dialog.
4. Text field (single line, editing keys), progress and level bars, slider (horizontal and vertical).
5. File picker: drive, directory list, filename field, OK and Cancel.
6. Canvas: a rectangle that shows a graphics layer through the UI, for content that really is pixels.

**How apps use it**

- **Widget records.** Each widget is a small record (type, parent, position, size, flags, text pointer, value) in
  an app-supplied table. A handle is its index, so no allocator is needed.
- **Static layouts.** A screen can be assembled in ca65 as data with macros, so building one is a single `ui_open`
  call.
- **Events.** `ui_poll` reads keys and mouse from the OOLIB event area, handles focus, clicks and scrolling, and
  returns app-level events: clicked, changed, selected, closed, menu chosen, each with the widget handle.
- **Redraw.** Widgets are marked dirty when their value or text changes; `ui_poll` repaints only dirty ones. Apps
  never draw.
- **Keyboard first.** Tab moves focus, Enter and Space activate, Alt opens menus, Escape cancels. The mouse is
  supported but never required, which suits CP/M habits.
- **Themes.** Colours and frame styles come from a theme table, so every app looks the same and one change
  restyles them all.
- **File picker.** It calls OODOS's directory functions itself, so apps get a filename back with one call.


## The API ##

**Proposed (phase U0, 2026-11-10), owner to confirm.** What U1 builds and an app is written to. The names below
become `oodos/include/ooui.inc`'s; the library is `build/ooui.lib`, an ar65 archive as `oolib.lib` is, with its
modules in `oodos/lib/ui*.s` (or a directory of its own, U1's choice). Numbers marked *provisional* may move in U1
before anything is built on them; the rest is the interface.

### Conventions ###

- **Calls are `JSR`s** to routines linked into the app, named `ui_*` as this record names them (`ui_open`,
  `ui_poll`); the constants are upper case (`UT_BUTTON`, `UE_CLICK`).
- **Registers, as the BDOS's:** a byte in A, a word in A (low) and X (high). A call that takes a handle and a word
  takes the handle in Y. A result comes back in A, or A and X.
- **Carry is the answer:** clear for done, set for refused with A an error (`UX_*`). Unlike the BDOS, the flags are
  not put back.
- **Every call changes A, X and Y** unless it says otherwise, and W1 (OOLIB's rule: OOUI draws through OOLIB, which
  maps W1 itself). The decimal flag is clear on return and the interrupt flag as it was.
- **The W1 contract holds:** everything OOUI reads while drawing (the widget table, the texts, the theme, the
  accelerator table, the event area, the picker's buffers) is in `$0300-$3FFF` or `$8000-$BFFC`, never
  `$4000-$7FFF`.
- **No call from a hook** (functions 80's frame or tick hook, 82's ES5503 hook). OOUI runs only in the program's
  own time.
- **No BDOS call** except in `ui_open`, `ui_close` and `ui_resume` (functions 80, 81 and 83: the interface, the
  mouse, the font, the pointer) and the file picker's directory reads (functions 17, 18, 26, 32, 49, 51 and 52),
  which run inside `ui_poll` a few at a time.
- **No blocking call.** A message box or the picker is opened by one call and answered by an event from `ui_poll`,
  so an app that feeds a stream or runs a game keeps its frame.

### Handles ###

- A handle is a record's index in the app's table, **0-239** (`$00-$EF`).
- **`$F0-$FE` are OOUI's own** built-in dialogs: `UI_MSG` (`$F0`) the message box, `UI_PICKER` (`$F1`) the file
  picker; the rest reserved.
- **`UI_NONE` (`$FF`)** is no widget: a top-level record's parent, an event with no widget.
- A parent comes before its children in the table, so a pass from 0 up draws back to front and a pass from the end
  down hit-tests front to back.

### Widget records ###

A record is 16 bytes (`UW_LEN`), so record *h* is at the table + *h* × 16. The table is the app's memory, and OOUI
writes `UW_FLAGS`'s dirty bit, `UW_STATE` and the values the user changes. An app may write any field itself and
then call `ui_dirty`.

| Offset | Name | What it is |
| --- | --- | --- |
| +0 | `UW_TYPE` | `UT_*` (below) |
| +1 | `UW_PARENT` | the parent's handle, `UI_NONE` for a top-level widget |
| +2 | `UW_X` | column, signed, from the parent's client area's left (inside its frame); negative counts from its right edge, -1 the last column |
| +3 | `UW_Y` | row, the same from the top and the bottom |
| +4 | `UW_W` | width in cells (a window's includes its frame); 0 fills to the parent's client edge |
| +5 | `UW_H` | height in cells; 0 fills |
| +6 | `UW_FLAGS` | `UF_*`: bits 0-3 common, bits 4-7 the type's own |
| +7 | `UW_STATE` | OOUI's (`US_*`); the app leaves it 0 |
| +8-9 | `UW_TEXT` | a 0-terminated string (0: none), or what the type says |
| +10-11 | `UW_VALUE` | the value, by type |
| +12-13 | `UW_A` | by type |
| +14-15 | `UW_B` | by type |

Positions and sizes are in **cells of the grid** `ui_info` reports (80×30 on the text backend), never pixels; the
theme says how big a cell and a frame are. A string is code page 437. A `&` before a letter marks it as the
widget's key (Alt+letter for a menu, the letter alone in a dialog) and draws it in the theme's accelerator
colour; `&&` is a `&`. In a list row, a TAB (`$09`) right-aligns what follows it.

**The types** (*provisional numbers*), in the order hw/ooui.md builds them; "-" is unused, 0:

| # | Type | `UW_TEXT` | `UW_VALUE` | `UW_A` | `UW_B` |
| --- | --- | --- | --- | --- | --- |
| 0 | `UT_NONE` | an unused record: skipped, so an app can keep handles | | | |
| 1 | `UT_DESKTOP` | - | - | - | - |
| 2 | `UT_WINDOW` | the title | - | - | - |
| 3 | `UT_PANEL` | the title (0: none) | - | - | - |
| 4 | `UT_LABEL` | the text | - | - | - |
| 5 | `UT_BUTTON` | the text | a toggle's state, 0 or 1 | - | - |
| 6 | `UT_CHECK` | the text | 0 or 1 | - | - |
| 7 | `UT_GROUP` | the title (0: none) | the chosen radio button, 0 its first | - | - |
| 8 | `UT_RADIO` | the text (a group's child) | - | - | - |
| 9 | `UT_MENUBAR` | - | - | - | - |
| 10 | `UT_MENU` | the title (the menu bar's child) | - | - | - |
| 11 | `UT_ITEM` | the text (a menu's child) | a check mark, 0 or 1 | +12 a key's HID usage shown, +13 its modifiers (`UK_*`) | - |
| 12 | `UT_SEP` | - (a menu's line) | - | - | - |
| 13 | `UT_VSCROLL` | - | the position, 0 to `UW_A` | the last position | the page (the thumb's size) |
| 14 | `UT_HSCROLL` | the same | | | |
| 15 | `UT_LIST` | the rows: an array of string pointers, or with `UF_ROUTINE` the row routine | the selection, `$FFFF` none | the rows' count | the top row shown |
| 16 | `UT_FIELD` | the buffer (0-terminated) | the cursor | +12 the buffer's size with its 0, +13 the first character shown | - |
| 17 | `UT_VBAR` | - | the level, 0 to `UW_A` | the top | - |
| 18 | `UT_HBAR` | the same | | | |
| 19 | `UT_VSLIDER` | - | the value, 0 to `UW_A` | the top | +14 a key's step, +15 the thumb's sprite (`UF_SPRITE`) |
| 20 | `UT_HSLIDER` | the same | | | |
| 21 | `UT_CANVAS` | - | - | - | - |

**The flags** (`UW_FLAGS`):

| Bit | Common | Window | Panel, group | Label | Button | List | Field | Slider |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 `$01` | `UF_HIDDEN` | | | | | | | |
| 1 `$02` | `UF_DISABLED`: drawn dim, takes no input | | | | | | | |
| 2 `$04` | `UF_DIRTY`: OOUI's, set by the calls that change a widget | | | | | | | |
| 3 `$08` | `UF_NOFOCUS`: Tab passes it by | | | | | | | |
| 4 `$10` | | `UF_CLOSE` close box | `UF_FRAME` | `UF_CENTRE` | `UF_DEFAULT` (Enter) | `UF_MULTI` marks | `UF_UPPER` folds to capitals | `UF_SPRITE` thumb |
| 5 `$20` | | `UF_MODAL` | | `UF_RIGHT` | `UF_CANCEL` (Escape) | `UF_SCROLL` its own scroll bar | | |
| 6 `$40` | | `UF_OVERLAY` on the overlay layer | | | `UF_TOGGLE` | `UF_ROUTINE` rows | | |
| 7 `$80` | | `UF_NOFRAME` | | | | | | |

`UW_STATE` (OOUI's): `US_PRESSED` `$01`, `US_HOT` `$02` (the pointer is over it), `US_OPEN` `$04` (a menu down),
`US_FOCUS` `$08`, `US_DRAG` `$10`.

**A list's rows.** Without `UF_ROUTINE`, `UW_TEXT` points to an array of `UW_A` string pointers. With it,
`UW_TEXT` is a routine OOUI calls (`JSR`) for each row it draws: A/X the row, Y the list's handle; it returns A/X a
0-terminated string, outside `$4000-$7FFF`, and Y a style (`UL_NORMAL` 0, `UL_CURRENT` 1, `UL_DIM` 2, `UL_MARKED`
bit 7, which a style adds). The routine may map W1 itself (OOUI maps it again after the call), may use the app's
zero page but not OOUI's, and may not call `ui_*`. A multiple-selection list keeps its marks in the app: OOUI asks
for them through the style.

**Static layouts.** A screen is assembled as data with macros, and one `ui_open` takes it. Each macro emits one
record and defines `id` as its handle (`.assert`ing that the parent came first); a text is a string literal, which
the macro puts in `RODATA` with its 0, or an address (0: none).

```
UI_BEGIN    name                                  ; the table's label; handles from 0
UI_DESKTOP  id
UI_WINDOW   id, parent, x, y, w, h, text, flags
UI_PANEL    id, parent, x, y, w, h, text, flags
UI_LABEL    id, parent, x, y, w, text, flags
UI_BUTTON   id, parent, x, y, w, text, flags
UI_CHECK    id, parent, x, y, w, text, value, flags
UI_GROUP    id, parent, x, y, w, h, text, value, flags
UI_RADIO    id, group, x, y, w, text, flags
UI_MENUBAR  id, parent
UI_MENU     id, menubar, text
UI_ITEM     id, menu, text, usage, mods, flags
UI_SEP      id, menu
UI_VSCROLL  id, parent, x, y, len, last, page, flags      ; UI_HSCROLL the same
UI_LIST     id, parent, x, y, w, h, rows, count, flags
UI_FIELD    id, parent, x, y, w, buffer, size, flags
UI_VBAR     id, parent, x, y, len, top, flags              ; UI_HBAR the same
UI_VSLIDER  id, parent, x, y, len, top, value, step, sprite, flags   ; UI_HSLIDER the same
UI_CANVAS   id, parent, x, y, w, h, flags
UI_END      name                                  ; name_COUNT: its records
```

For example, a window with a list and two buttons:

```
        UI_BEGIN    main
        UI_DESKTOP  W_DESK
        UI_WINDOW   W_MAIN, W_DESK, 10, 4, 60, 20, "Tunes", UF_CLOSE
        UI_LIST     W_SONGS, W_MAIN, 1, 1, -2, 14, song_row, 0, UF_ROUTINE | UF_SCROLL
        UI_BUTTON   W_PLAY, W_MAIN, 1, -2, 10, "&Play", UF_DEFAULT
        UI_BUTTON   W_QUIT, W_MAIN, -12, -2, 10, "&Quit", UF_CANCEL
        UI_END      main
```

### Opening and closing ###

`ui_open` takes A/X, a block of `UO_LEN` (16) bytes:

| Offset | Name | What it is |
| --- | --- | --- |
| +0-1 | `UO_AREA` | the event area the app opened (function 80's `FT_OPEN`), layout 2: 256 bytes advised (15 events) |
| +2-3 | `UO_TABLE` | the widget table |
| +4 | `UO_COUNT` | its records, 1-240 |
| +5-6 | `UO_THEME` | a theme (0: the default) |
| +7-8 | `UO_KEYS` | the accelerator table (0: none) |
| +9 | `UO_BASE` | the layer the widgets go on, 0 or 1 |
| +10 | `UO_OVER` | the layer for menus, dialogs and `UF_OVERLAY` windows, 0 or 1, or `$FF`: none, they go on the base layer and what they covered is drawn again when they close |
| +11 | `UO_SPRITE` | the first sprite OOUI may use (the pointer, then slider thumbs) |
| +12 | `UO_SPRITES` | how many, 0-16 (0: no pointer sprite) |
| +13 | `UO_FLAGS` | `UO_OWNMOUSE` `$01`: the app sets the mouse's mode and the pointer itself |
| +14-15 | | reserved, 0 |

It checks OODOS's interface (function 80's `FT_INFO`: version 4 or more, with `OA_INPUT` and `OA_DMA`) and the
area (layout 2); takes its video memory; makes its font; sets the layers it was given; puts the theme's colours in
OOLIB's palette shadow (`GX_PALINIT` first if the app hasn't); sets the mouse to absolute mode with limits 319 and
239 (function 81's `IN_MOUSE`) and the pointer on `UO_SPRITE` (function 83's `DM_POINTER`), unless
`UO_OWNMOUSE`; checks the table; marks every visible widget dirty; and gives the focus to the first that takes it.
Nothing is drawn until `ui_poll`. Errors: `UX_VERSION`, `UX_AREA`, `UX_MEMORY`, `UX_TABLE`.

`ui_close` frees OOUI's video memory, turns the pointer off and shows the console's screen (`GX_CONSOLE`). The
warm boot does the same for a program that ends without it. The event area stays the app's to close.

### The calls ###

| Call | In | Out, and what it does |
| --- | --- | --- |
| `ui_open` | A/X the block above | carry clear; or carry set, A `UX_*` |
| `ui_close` | | |
| `ui_poll` | | A the event (`UE_*`), X its widget, Y as the event says, and `UI_EV` (below). Carry clear |
| `ui_show` | A a handle | shown with its children; a `UF_MODAL` window takes the input and the focus |
| `ui_hide` | A a handle | hidden with its children; what it covered drawn again. `UI_PICKER` or `UI_MSG` closes it, with no event |
| `ui_flags` | Y a handle, A the bits to set, X the bits to clear | A the flags now; dirty. Not `UF_DIRTY` or `UF_HIDDEN` (`ui_show`, `ui_hide`) |
| `ui_settext` | Y a handle, A/X a string | dirty |
| `ui_setvalue` | Y a handle, A/X the value | held to the widget's range; dirty. No event |
| `ui_getvalue` | Y a handle | A/X the value |
| `ui_setrange` | Y a handle, A/X a list's count or a bar's, slider's or scroll bar's top | the value held to it; dirty |
| `ui_dirty` | A a handle | it and its children drawn again at the next idle `ui_poll` |
| `ui_redraw` | | everything drawn again (after the app has had the screen) |
| `ui_focus` | A a handle | carry set if it can't take it (hidden, disabled, `UF_NOFOCUS`, or under a modal window) |
| `ui_getfocus` | | A the handle, `UI_NONE` none |
| `ui_rect` | A a handle | `UI_RECT`: +0 column, +1 row, +2 width, +3 height, on the screen in cells; carry set if hidden |
| `ui_info` | | A the grid's columns, X its rows, Y the backend (`UB_TEXT` 0, `UB_BITMAP` 1) |
| `ui_theme` | A/X a theme (0: the default) | colours and metrics changed; everything drawn again |
| `ui_msgbox` | A/X a block (below) | carry set (`UX_BUSY`) if one is up |
| `ui_pick` | A/X a block (below) | carry set (`UX_BUSY`) if the picker is up |
| `ui_relist` | | the picker's directory read again (after the app mounted a drive) |
| `ui_suspend` | | OOUI stops drawing; `ui_poll` returns every key, button and wheel event as `UE_RAW`; the layers are the app's |
| `ui_resume` | | the layers and the pointer set again; everything drawn again |

Errors (`UX_*`, *provisional numbers*): `UX_VERSION` 1 (OODOS's interface below 4, or no input or DMA), `UX_AREA` 2
(no event area, or layout 1), `UX_MEMORY` 3 (no video memory), `UX_TABLE` 4 (an unknown type, a parent after its
child, a menu item outside a menu, more than 240 records), `UX_HANDLE` 5 (a handle past the table, or a call the
type doesn't take), `UX_BUSY` 6.

### Events ###

`ui_poll` returns one event a call, so the app calls it until it returns `UE_NONE` and then does its frame's work.
It reads the event area's queue itself, from `EV_QTAIL` to `EV_QHEAD`, and moves `EV_QTAIL` on: while OOUI is
open, **OOUI is the queue's only reader**, and what it doesn't use reaches the app as `UE_RAW`. Before it returns
`UE_NONE`, it draws every dirty widget, so a change shows in the frame after it.

| # | Event | X | Y | `UV_VALUE`, and when |
| --- | --- | --- | --- | --- |
| 0 | `UE_NONE` | `UI_NONE` | 0 | nothing waits; the dirty widgets drawn |
| 1 | `UE_CLICK` | the button | 0 | a button pressed (click, Enter, Space, its key or its accelerator); a toggle's new state |
| 2 | `UE_CHANGE` | the widget | 0 | a check box, a group (the radio button chosen), a slider, a scroll bar, a list's selection, a field. `UV_LIVE` while a drag or an edit goes on; a field's last comes at Enter or when the focus leaves |
| 3 | `UE_SELECT` | the list | 0 | a row chosen: Enter, or a double-click; the row |
| 4 | `UE_MARK` | the list | 0 | a `UF_MULTI` list's row marked or unmarked (Space, a click): the row. The app changes its mark and calls `ui_dirty` |
| 5 | `UE_MENU` | the item | its menu | an item chosen |
| 6 | `UE_CLOSE` | the window | 0 | its close box, or Escape: the app hides it (`ui_hide`) or keeps it. `UI_MSG` closes itself, and says the button: 0 OK or Yes, 1 Cancel or No |
| 7 | `UE_FOCUS` | the widget | 0 | the focus moved there |
| 8 | `UE_KEY` | the focus | the HID usage | a key pressed that no widget or accelerator took; `UV_MODS`, `UV_CHAR` |
| 9 | `UE_WHEEL` | under the pointer | 0 | wheel steps no list or scroll bar took, signed |
| 10 | `UE_CANVAS` | the canvas | 0 | the pointer on a canvas: `UV_KIND`, `UV_X`, `UV_Y` (wheel steps in `UV_VALUE`) |
| 11 | `UE_PICK` | `UI_PICKER` | 0 | 0 chosen (the picker closed), 1 cancelled (closed), 2 its extra button (still open) |
| 12 | `UE_RAW` | `UI_NONE` | the kind (`EK_*`) | an event OOUI doesn't use (sound, controllers, devices), its 8 bytes in `UI_RAW` |
| 13 | `UE_MODAL` | the window | 0 | 1 the first modal window shown, 0 the last hidden: the app may dim its own layer |

`UI_EV` (12 bytes, `UV_*`): +0 `UV_CODE`, +1 `UV_HANDLE`, +2-3 `UV_VALUE`, +4-5 `UV_X` and +6 `UV_Y` (the
pointer, in pointer units from the widget's top left), +7 `UV_KIND` (a canvas's: `UC_PRESS` 1, `UC_RELEASE` 2,
`UC_MOVE` 3 while a button is held, `UC_WHEEL` 4), +8 `UV_MODS` (`EV_MODS`'s then), +9 `UV_USAGE`, +10 `UV_CHAR`
(ASCII, 0 none), +11 `UV_FLAGS` (`UV_LIVE` `$01`, `UV_DOUBLE` `$02`, `UV_KEYED` `$04`: by the keyboard).

**Where a key goes.** First to an open menu or the modal window, then to the focused widget (a list takes Up,
Down, PgUp, PgDn, Home, End, Enter and Space; a field takes printable keys, Left, Right, Home, End, Backspace and
Delete; a slider its arrows), then Tab and Shift+Tab (focus), Alt (the menu bar), the `&` letters of the window
with the focus, then the accelerator table, then the app as `UE_KEY`. OOUI repeats the keys a widget takes while
they are held (`EV_KEYS`, timed by `EV_FRAMES` with the theme's times), since raw events don't repeat. It turns usages
into ASCII with a table of its own (a US layout), as the USB card's KEY would.

**The accelerator table** (`UO_KEYS`): entries of 4 bytes (`UK_*`), ended by a usage of 0. +0 the HID usage, +1
the modifiers that must be held (`UK_CTRL` `$11`, `UK_SHIFT` `$22`, `UK_ALT` `$44`: either side's bit of
`EV_MODS`; 0 none), +2 a handle, +3 the action: `UA_PRESS` 0 (as if clicked: a button's `UE_CLICK`, an item's
`UE_MENU`, a check box toggled), `UA_UP` 1 and `UA_DOWN` 2 (a slider or scroll bar by its step, a list's
selection by a row), `UA_FOCUS` 3.

**The mouse.** The left button presses, drags and double-clicks (two presses on one widget within the theme's
time); the wheel scrolls the list or scroll bar under the pointer, else `UE_WHEEL`. OOUI reads `EK_BUTTON` and
`EK_WHEEL` from the queue and the position from `EV_MX` and `EV_MY`; it leaves `EV_MWHEEL` to the app.

### Message box and file picker ###

**`ui_msgbox`** takes a block (`UM_*`): +0-1 the text (lines split at CR, `$0D`), +2-3 the title, +4 the buttons
(`UM_OK` 0, `UM_OKCANCEL` 1, `UM_YESNO` 2), +5 the default button (0 or 1). It is a modal window on the overlay
layer; Y and N answer a Yes/No box, Escape is its last button. The answer is `UE_CLOSE` with X `UI_MSG`.

**`ui_pick`** opens the file picker, a modal window: drive buttons, the user number, a directory list, a filename
field, OK and Cancel. It takes a block of 20 bytes (`UP_*`):

| Offset | What it is |
| --- | --- |
| +0-1 | the title |
| +2-3 | the types shown: three-letter types one after another, ended by 0 (`"MP3SIDWAV", 0`); 0 every file |
| +4 | `UP_MULTI` `$01` several files; `UP_NEW` `$02` a name typed that isn't there (saving); `UP_DRIVES` `$04` drive buttons; `UP_USERS` `$08` user numbers 0-15 |
| +5 | the drive to start on (0 the current, 1 A:) |
| +6 | the user to start on (`$FF` the current) |
| +7-8 | the result buffer |
| +9 | its room, in entries |
| +10 | the entries chosen (OOUI writes it) |
| +11-12 | a column routine, 0 none: A/X an entry, Y its row; it returns A/X a short string for the row's right, or 0 for "not yet" (asked again later), so a slow look inside a file can be spread over frames |
| +13-14 | an extra button's text (0 none), such as MOUNT |
| +15-16 | a work buffer for the listing |
| +17-18 | its length: 16 bytes a file listed |
| +19 | reserved, 0 |

An entry (16 bytes, `UP_E*`): +0 the drive (1 A:), +1 the user, +2-12 the name and type as an FCB holds them
(8 + 3, spaces, the attribute bits clear), +13-15 reserved. An app makes an FCB of it by putting +0 at FCB+0 and
+2-12 at FCB+1, and sets the user (function 32) before opening it.

The picker reads the directory with functions 17 and 18, one directory record (four entries) a `ui_poll`, so
listing never takes a frame; OODOS keeps a search going across other calls (function 18). It lists each file once,
however many extents it has. It names the drive in its FCB, so the current drive never changes, and it sets the
user (function 32), the DMA address and the DMA segment for each read and puts the app's back after it (functions
26, 51 and 52, and the SCB's `$3C` by function 49). The answer is `UE_PICK`.

### Themes ###

A theme is a table of 64 bytes (`TH_*`). Colours are palette entries 0-15 in a 16-colour group (the layer's
PALOFS), so one theme serves both backends; 0 is transparent, as the card makes it.

| Offset | Name | What it is |
| --- | --- | --- |
| +0 | `TH_VERSION` | 1 |
| +1 | `TH_FLAGS` | `TF_SHADOW` `$01` drop shadows; `TF_DIM` `$02` the base layer on `TH_DIMGROUP` while a modal window is up |
| +2-3 | `TH_PALETTE` | 16 colours for the base group, 2 bytes each as `GX_PALSET` takes them (GGGGBBBB, 0000RRRR); 0: the BIOS's CGA set |
| +4-5 | `TH_OVERPAL` | 16 for the overlay group; 0: the base group's |
| +6 | `TH_GROUP` | the base layer's group (PALOFS, 0-15) |
| +7 | `TH_OVERGROUP` | the overlay layer's |
| +8 | `TH_DIMGROUP` | the base layer's while a modal is up |
| +9 | `TH_DESKCHAR` | the desktop's character (`$B0` a light shade, `$20` plain) |
| +10-17 | `TH_METRICS` | in cells: +10 a frame's width (1), +11 a title's rows (0: on the frame's top line), +12 a button's padding each side (1), +13 a scroll bar's width (1), +14 a shadow's columns (2), +15 its rows (1), +16 the space between menu titles (1), +17 reserved |
| +18-25 | `TH_PIXELS` | the bitmap backend's (the text backend ignores them): +18 a cell's width (8), +19 its height (16), +20 a line's width (1), +21 a bevel's (2), +22-25 reserved |
| +26-29 | `TH_TIMES` | in frames: +26 a key's delay before it repeats (30), +27 between repeats (2), +28 a double-click's (24), +29 the cursor's blink (16) |
| +30-33 | `TH_FRAMES` | a frame's style (`TS_NONE` 0, `TS_SINGLE` 1, `TS_DOUBLE` 2, `TS_HEAVY` 3) for +30 a window, +31 a dialog, +32 a panel, +33 a menu; the backend draws each its own way |
| +34-62 | `TH_COLOURS` | an attribute byte (background in bits 7-4, foreground in 3-0) for each role, `TC_*` below |
| +63 | | reserved, 0 |

The roles, in order from +34: `TC_DESKTOP`, `TC_FRAME`, `TC_TITLE`, `TC_TITLEOFF` (an inactive window's),
`TC_CLIENT`, `TC_LABEL`, `TC_BUTTON`, `TC_BUTTONFOCUS`, `TC_BUTTONDOWN`, `TC_DISABLED`, `TC_ACCEL`, `TC_CHECK`,
`TC_MENUBAR`, `TC_MENU`, `TC_MENUSEL`, `TC_LIST`, `TC_LISTSEL`, `TC_LISTCUR`, `TC_LISTDIM`, `TC_LISTMARK`,
`TC_FIELD`, `TC_FIELDFOCUS`, `TC_SCROLL`, `TC_THUMB`, `TC_BAR`, `TC_BAREMPTY`, `TC_SHADOW`, `TC_DIALOG`,
`TC_DIALOGFRAME`: 29.

### Text backend: resources ###

**Layers.** The widgets go on `UO_BASE` and the overlays on `UO_OVER`, each an 80×30 text layer (CONFIG text, 80
columns) with OOUI's font, and PALOFS the theme's groups. With both layers OOUI's, nothing is behind layer 0 but
the border, so a **canvas needs the app's own layer**: `UO_BASE` 1, `UO_OVER` `$FF`, the app's tiles or bitmap on
layer 0. A drop shadow on the overlay layer copies the characters under it from the base layer in `TC_SHADOW`.

**Glyphs.** OOUI copies the console's font (units 1-4, by function 83; a copy's source may be the console's) and
redefines these codes (*provisional*; `UG_*` in ooui.inc), keeping CP437's box drawing (`$B3-$DA`), shades
(`$B0-$B2`), blocks (`$DB-$DF`) and arrows (`$10`, `$11`, `$18-$1F`):

| Codes | Redefined as |
| --- | --- |
| `$01-$0F` | vertical bars filled 1/16 to 15/16 from the bottom (16/16 is `$DB`) |
| `$12-$17`, `$1C` | horizontal bars filled 1/8 to 7/8 from the left (8/8 is `$DB`) |
| `$7F`, `$9B` | a check box empty, ticked |
| `$9C`, `$9D` | a radio button off, on |
| `$9E`, `$9F` | a folder, a drive (the picker) |

So a vertical bar or slider *n* cells long draws 16*n* levels, and a horizontal one 8*n*. CP437's accented letters
(`$80-$9A`, `$A0-$A8`) stay, for names that have them.

**Video memory.** One piece of 16 KB, on a 16 KB boundary, so that one window mapping reaches all of it:

| Offset | Size | What |
| --- | --- | --- |
| `$0000` | 5 KB | the base layer's map (4,800 bytes) |
| `$1400` | 5 KB | the overlay layer's map (not used when `UO_OVER` is `$FF`) |
| `$2800` | 2 KB | sprite images: the pointer and slider thumbs, 16×16 at 4 bits, 128 bytes each |
| `$3000` | 4 KB | the font |

Each part starts on 1 KB, which today's base registers need. The palette and the sprite table are OOLIB's
(`GX_PALINIT`, `GX_SPRINIT`), shared with the app; OOUI writes only its groups and its sprites.

**What OOUI expects from OOLIB's video-memory allocation.** OODOS's function 84 and OOLIB's allocation for
revision 2 are being written now; OOUI needs, under whatever names they get:

1. **Allocate** a size in 256-byte pages (a word), with an alignment (16 KB here): the card address (24 bits), or
   carry set if none is free.
2. **Free** it.
3. **Reach it:** a card address's 16 KB page mapped into W1 (or another window), with the CPU address of its
   start; and the program's own block put back.
4. **Copy** program memory to card memory, and card memory to card memory (the font), by DMA or the port.
5. **Point a register at it:** a layer's MAPBASE and TILEBASE, and a sprite's image, set from a card address.

On today's card, OOLIB has all five: `GX_ALLOC` (1 KB units; OOUI asks for 32 and frees the half it doesn't need
if the run isn't on a 16 KB boundary), `GX_FREE`, `GX_BLOCK` and `GX_W1BACK`, `DM_COPY`, and `GX_LAYER` and
`GX_SPRIMG`.

**The pointer and hit-testing.** The pointer is OODOS's sprite (`DM_POINTER`), in the sprites' 320×240 space, the
mouse in absolute mode with limits 319 and 239. A cell is 4×8 pointer units, so the cell under the pointer is
column `EV_MX` / 4 and row `EV_MY` / 8. OOUI tests widgets from the table's end down, skipping hidden ones and
those under a modal window, and takes the last child that holds the cell. A slider's value is taken in pointer
units along its track, finer than a cell (a cell holds 4 across, 8 down), and its thumb, as a sprite, is put to the
pointer unit (`GX_SPRPOS`).

### Zero page and memory ###

- **Zero page:** 8 bytes of OOUI's own (`UIP`, `UIQ`, `UIR`, `UIT`, two each), in modules of their own as OOLIB's
  are (`lib/zpui*.s`, after Plan B), so ld65 places them in the app's `$00-$EF` and an overlay is given its
  caller's addresses (`tools/libzp.py`). They hold nothing between calls: the app may use them between calls, but
  not in a row or column routine. OOLIB's `GXP`, `GXQ` and `GXT` (6 bytes) come with the graphics modules OOUI
  links. Nothing in `$F0-$FF`.
- **The app's memory:** OOUI's code and data are linked into the app's image (`CODE`, `RODATA`, `BSS`), outside
  `$4000-$7FFF` as the W1 contract wants. Its state is about 100 bytes of `BSS`; the message box and the picker
  bring their own records (about 128 and 640 bytes) only when linked. *Target, measured at U1's and U2's gates:*
  about 4 KB of code for U1's core and text backend, about 3 KB more for U2.
- **The event area** is the app's (function 80's `FT_OPEN`, layout 2). OOUI reads `EV_QHEAD`, `EV_QTAIL`,
  `EV_QSLOTS` and `EV_QUEUE` (and writes `EV_QTAIL`), `EV_MODS`, `EV_DEVICES` (no mouse: no pointer), `EV_MX`,
  `EV_MY`, `EV_MBUTTONS`, `EV_KEYS` (repeat) and `EV_FRAMES` (times). It leaves `EV_QLOST`,
  `EV_MWHEEL`, the controllers, the paddles and the sound part to the app.
- **Interrupts, DMA, the VIA:** none of its own. It uses function 83's copies and OOLIB's vertical-blank slots
  through `GX_PALSHOW` and `GX_SPRSHOW`, so the app's event area must be the one OOLIB was given (`GX_INIT`).

### Against both backends ###

The API has no text-mode numbers in it: positions and sizes are cells of the grid `ui_info` reports, colours are
palette entries, frames are styles, and the backend turns them into cells or pixels. For the bitmap backend:

- **The theme's `TH_PIXELS`** (a cell's size, lines, bevels) are used, and `TH_FRAMES`' styles are drawn as lines
  and bevels instead of CP437 sets. A theme for both carries both; colours, metrics in cells and times are the
  same.
- **The grid differs:** bitmap 512 is 512 pixels wide, 64 cells of 8, so `ui_info` says 64×30; a 640×480 mode on
  revision 2 would give 80×30 again. A layout that uses negative positions and sizes of 0 (from the edges, fill)
  fits either; one written for 80 columns needs the 640 mode.
- **Text** is drawn by `GX_TEXT` in OOUI's font; bars and check boxes are pixels (a bar's levels become its
  pixels); no glyph is redefined.
- **Layers:** one bitmap on `UO_BASE`; the overlay as a second bitmap where memory allows (revision 2), else
  `UO_OVER` `$FF` and covered widgets drawn again, as the text backend's one-layer mode does.
- **Hit-testing** is the same with the theme's cell size: column (2 × `EV_MX` − 64) / 8 on bitmap 512, which starts
  at x 64.
- **Memory:** 120 KB for a bitmap 512 screen: half the units a program has on today's card, little on revision 2.

So, for an app, only the theme (and the grid's width) changes; the records, the calls, the events, the macros and
the picker are the same.


## Text backend ##

The first backend draws on the 80×30 text layer, which is cheap (4.8 KB a screen, 2 bytes a cell) and already full
resolution and colour.

- **Glyphs.** The BIOS font is the IBM VGA font, so code page 437's box-drawing, shading and arrow characters are
  there for frames, scroll bars and shadows. OOUI loads its own copy of the font and redefines spare codes for
  checkboxes, radio buttons and small icons.
- **Sub-cell bars.** A cell is 8×16 pixels. Sixteen redefined glyphs, filled 1/16 to 16/16 from the bottom, give
  vertical bars and sliders 16 levels per cell (an EQ slider 8 cells tall has 128 steps); eight more give
  horizontal bars 8 levels per cell.
- **Two layers.** Layer 0 holds windows; layer 1, also 80×30 text, holds menus, dialogs and tooltips. Cells on
  layer 1 with background 0 are transparent, so a menu opens over a window without saving and restoring what's
  under it.
- **Sprites for precision.** Slider thumbs and the pointer can be sprites, positioned to the pixel.
- **Canvas.** The canvas widget leaves its cells transparent so a graphics layer behind shows through: bitmap 256
  or tiles, where the app draws a visualizer or waveform at pixel resolution.
- **Colours.** 16 attribute colours from a theme palette, starting from the BIOS's CGA set. PALOFS can give
  dialogs their own colour set.
- **Pointer.** The OODOS pointer sprite, already driven from the frame handler. It lives in the sprites' 320×240
  space, so a cell is 4×8 pointer units; hit-testing divides by those.
- **Memory.** The screens and OOUI's font live in video memory allocated through OOLIB. OODOS's console keeps its
  own region and comes back on the warm boot.


## Bitmap backend ##

A later backend for a Windows 3.1 or GEOS look, only if the text backend proves too limiting (decided at U3's
gate).

- **The surface.** Bitmap 512 today (512×480, 4 bits a pixel, centred with BORDER either side). With revision 2's
  memory a full-width 640×480 mode becomes possible; that is a separate decision.
- **Drawing.** Built on OOLIB's `gx` bitmap calls (rectangle, fill, line, text on a bitmap).
- **Cost.** Repainting a large window is visibly slower than in text mode; the drawing assist below fixes most of
  that.
- **Same API.** Apps written for the text backend run unchanged; only the theme's metrics differ.


## Optional drawing assist ##

If the bitmap backend needs speed, add primitives in hardware, the classic accelerator model: primitives in
hardware, widgets in the library.

1. **2D transfers:** width, height and pitch, so a rectangle fill, a window copy or a scroll is one transfer.
2. **Glyph expansion:** 1-bit font data to 4-bit pixels with a foreground and background colour.

On today's snooped card these belong in the DMA controller, which writes real RAM. With revision 2's private memory
the video card itself is the better place. Both are optional; the text backend never needs them.


## Apple II mode and existing software ##

- **Apple II mode.** OOUI runs only under OODOS in native mode. Apple mode never reaches the card's layers, palette
  or registers.
- **OODOS console.** OOUI leaves the console's memory alone; the warm boot restores the console as it does today.
- **Interrupts.** OOUI needs none of its own. It reads input from the OOLIB event area OODOS already fills.
- **Other programs.** The BIOS, the demo, GW-BASIC and OOLIB games keep using the layers directly. A game can put
  an OOUI dialog on layer 1 over its own layer 0.


## Emulator and testing ##

The text and bitmap backends need no emulator changes: layers, sprites, DMA and the USB mouse are all emulated.

- **Golden frames.** Scripted test programs build screens and drive input; `--screen` PPMs are compared against
  stored frames for each widget and theme.
- **Input scripts.** Keys come through the existing piped input; the WebUI covers mouse testing until a scripted
  mouse is added.
- **Debugger.** ld65 labels (`--symbols`) make OOUI readable at `debug>`; a `ui` command that dumps the widget
  table is a nice-to-have.


## What OO-Amp needs from OOUI ##

**Proposed (U0, 2026-11-10), to be extended when OO-Amp is ported (U3).** From OO-Amp as it is today (its
`SPEC.md`, `oa_ui.s`, `oa_list.s`, and the overlays in `gui/`): what OOUI must do for the port to make it smaller,
and where in the API above it is. Items marked *gap* aren't in the API yet.

1. **Its own layer under OOUI's.** OO-Amp's chrome, LED digits and spectrum are pixel art in a tile layer (layer 0)
   with the words on a text layer over it (layer 1). OOUI opens with `UO_BASE` 1 and `UO_OVER` `$FF`, its
   backgrounds transparent (colour 0 in the theme), and the dialogs on the same layer, the widgets under them drawn
   again when they close.
2. **Its look.** A theme of its own (OO-Amp's 15 colours from its skin sheet), and its palette swap for the dimmed
   screen behind a dialog: `UE_MODAL` tells it when to swap.
3. **Sliders with sprite thumbs:** seek, volume, balance, 11 EQ bands and the playlist's scroll bar, 15 thumbs
   and the pointer, all 16 of `UO_SPRITES`, dragged with live values (`UV_LIVE`) and keyed. Seek disabled when
   the plugin can't seek (`UF_DISABLED`).
4. **Its keys.** Z X C V B, S, R, G, F, M, J, L, H, O, D, the brackets and the rest as the accelerator table;
   Up and Down as the volume slider's `UA_UP`/`UA_DOWN`, which the playlist takes first when it has the focus,
   as OO-Amp's own `A_UP` does today; raw keys with no console repeat; Tab between the player and the playlist.
5. **A frame every frame.** OO-Amp feeds a stream and a vis page each frame, so no OOUI call may block: the
   message box, the dialogs and the picker are answered by events, and the picker reads the directory a record
   at a time.
6. **Events OOUI doesn't use,** passed on (`UE_RAW`): the sound card's mailbox bytes when the app queues them, and
   the devices plugged in or out.
7. **The playlist,** a list whose entries are in banked blocks OO-Amp maps in W1: the row routine (`UF_ROUTINE`),
   which copies the row out of W1 before it returns; styles for the current, a bad and a selected row; the length
   right-aligned (a TAB); 11 rows, PgUp and PgDn, double-click or Enter to play (`UE_SELECT`), Delete as `UE_KEY`,
   the wheel scrolling it under the pointer and the volume elsewhere (`UE_WHEEL`).
8. **Load Files** as the picker: drives A-G, users 0-15 (`UP_DRIVES`, `UP_USERS`), several files (`UP_MULTI`),
   the types its plugins claim (the filter), a title for each row from its plugin's PROBE (the column routine,
   spread over frames), and MOUNT (the extra button, then `ui_relist`). *Gap:* ADD ALL (every file listed), and
   the user's choice kept marked across ADD presses; U2 decides whether these are the picker's or OO-Amp's.
9. **Click zones:** the time (elapsed or remaining) and the spectrum (the next mode) as canvases (`UE_CANVAS`).
10. **Toggle buttons:** Shuffle, Repeat, EQ and PL (`UF_TOGGLE`).
11. **CONFIRM QUIT:** a Yes/No message box, answered by Y or N.
12. **Help and Settings:** a window of labels, and lines chosen by Up and Down and changed by Left and Right.
    *Gap:* a choice widget (a value from a short list of texts, changed by Left and Right); U2, or labels and
    accelerators in OO-Amp.
13. **Panels swapped:** the EQ panel and the SID voices panel in one place (`ui_hide`, `ui_show`).
14. **The marquee and the status line:** a label whose text changes every 8 frames, 42 cells drawn again.
15. **The full-screen visualizer,** which takes the screen and gives it back: `ui_suspend` and `ui_resume`.
16. **Overlays and zero page.** OO-Amp's GUI is in `.PRL` overlays (function 59) that have no zero page of their
    own: OOUI's zero page in `zpui*` modules, as OOLIB's, lets an overlay be given the core's. Its core's W0 is
    nearly full, so ar65's linking only what is used, and the sizes at U1's and U2's gates, matter.


## The plan ##

Six phases, all software, each closed by a gate:

| Phase | Work | Gate |
| --- | --- | --- |
| U0 | API spec: widget records, events, themes, file picker; checked against OO-Amp and both backends | API decided, open questions settled |
| U1 | Core and text backend: desktop, windows, labels, buttons, menus, focus, themes, glyphs, layer 1 overlays, pointer hit-testing | Golden frames pass |
| U2 | Scroll bars, list boxes, message boxes, text fields, sliders, bars, canvas; file picker on OODOS directory calls | File picker works on real disks |
| U3 | OO-Amp rebuilt on OOUI; gaps fixed in OOUI, not the app; before and after code size compared | OO-Amp clearly smaller; decide on the bitmap backend |
| U4 | Bitmap backend (optional), drawing assist if repaints are slow | Repaints fast enough to feel native |
| U5 | Programmer's Guide chapter, examples, docs/README.md phases, a second app | A second app built on OOUI |


## Risks ##

| Risk | Mitigation |
| --- | --- |
| Library grows too big to link into every app | ar65 links only used modules; move the core into a shared banked block or RSX if size bites |
| API leaks text-mode assumptions | Positions and sizes in abstract units; theme supplies metrics; review against both backends in U0 |
| Repaints are slow in bitmap mode | Dirty-widget redraw; the drawing assist as the fix |
| Apps still special-case UI | OO-Amp as the first port; add what it needed to OOUI rather than to the app |
| Text-mode look feels too plain | Theme work and custom glyphs first; bitmap backend if it's still not enough |


## Open Questions ##

**Recommended answers (U0, 2026-11-10), owner to confirm.** Each answer follows its question.

1. Name: OOUI (proposed) or OOTK?
   **Recommended, owner to confirm:** OOUI, with the prefix `ui`, as the decisions above and the API use it.
2. Linked into each app, or a shared banked block or RSX from the start?
   **Recommended, owner to confirm:** linked into each app first, as OOLIB is and as the decisions above say; ar65
   links only the modules an app uses. A shared banked block (FARCALL) is reconsidered at U3's gate with OO-Amp's
   measured sizes, as the risks table says.
3. Which theme look: Turbo Vision blue, a grey Windows 3.1 feel, or an OO-1 house style?
   **Recommended, owner to confirm:** an OO-1 house style as the default theme, taken from OO-Amp's skin-sheet
   colours, so the proving app and the toolkit look like one machine; and a Turbo Vision blue theme beside it in
   U1, which costs one 64-byte table and proves that a theme restyles everything. The grey Windows 3.1 feel waits
   for the bitmap backend, where its bevels can be drawn.
4. Does OO-Amp's visualizer stay app-drawn on a canvas, or become an OOUI widget?
   **Recommended, owner to confirm:** it stays app-drawn on a canvas, as the text backend's canvas is meant for
   ("a visualizer or waveform at pixel resolution"); OOUI's bars cover simple meters such as a VU. Revisited only
   if a second app wants the same spectrum.
5. Is the bitmap backend wanted at all, or is a well-themed text UI enough for OO-1?
   **Recommended, owner to confirm:** decided at U3's gate, as the plan says, once OO-Amp runs on the text
   backend. The API is kept free of text-mode numbers ([Against both backends](#against-both-backends)), so
   nothing is lost by waiting.
6. Build the drawing assist only if the bitmap backend is built?
   **Recommended, owner to confirm:** yes: the text backend never needs it, so it is built only with the bitmap
   backend (U4), and only if that backend's repaints are measured too slow; with revision 2, on the video card.

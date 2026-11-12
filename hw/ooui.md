# OOUI: the OO-1's UI toolkit #

Nov 10, 2026 · @Jefferson

**Status: proposed (2026-11-10).** Phase U0 (below) settled the API and the open questions, as OOLIB's plan did.
**U0 accepted by the owner (2026-11-11):** [the API](#the-api), [what OO-Amp needs](#what-oo-amp-needs-from-ooui)
and the six recommended answers to the [open questions](#open-questions) stand, and U0's gate is met. U1, the core
and the text backend, builds the API as it is written here.
The video memory it allocates from is video card revision 2's ([video-rev2.md](video-rev2.md)): the program's part
of card memory, handed out by OOLIB ([oolib.md](oolib.md), "Video card revision 2"; `oodos/include/vmem.inc`).
A richer, LVGL-based GPU was considered and parked for a future machine: [future/gpu-lvgl.md](future/gpu-lvgl.md).


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
| Where it runs | Entirely on the 6502, linked into the app. No resident OODOS service is needed at first. *U3a (proposed):* a banked library the program loads and reaches by FARCALL ([below](#u3a-ooui-as-a-banked-library)) |
| Display | 640×480, existing layer types, palette-based. No new modes |
| Backends | Text layer first (80×30); bitmap later if needed |
| Video memory | Card memory of video card revision 2 ([video-rev2.md](video-rev2.md)), allocated through OOLIB (`GX_VALLOC`) |
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

**Proposed (phase U0, 2026-11-10); accepted by the owner (2026-11-11).** What U1 builds and an app is written to. The names below
become `oodos/include/ooui.inc`'s; the library is `build/ooui.lib`, an ar65 archive as `oolib.lib` is, with its
modules in a directory of its own, `oodos/ui/` (U1's choice: `ui/NAME.s`, its zero page in `ui/zpNAME.s`).
Numbers marked *provisional* may move in U1 before anything is built on them; the rest is the interface. What U1
built, and the few places it changed this text, are in [U1](#u1-the-core-and-the-text-backend).

### Conventions ###

- **Calls are `JSR`s** to routines linked into the app, named `ui_*` as this record names them (`ui_open`,
  `ui_poll`); the constants are upper case (`UT_BUTTON`, `UE_CLICK`).
- **Registers, as the BDOS's:** a byte in A, a word in A (low) and X (high). A call that takes a handle and a word
  takes the handle in Y. A result comes back in A, or A and X.
- **Carry is the answer:** clear for done, set for refused with A an error (`UX_*`). Unlike the BDOS, the flags are
  not put back.
- **Every call changes A, X and Y** unless it says otherwise, and W1 and aperture slot 1 (OOLIB's rule: OOUI draws
  through OOLIB, whose `GX_VMAP` puts one of the program's card banks in slot 1 and slot 1's window, block 33, in
  W1). The decimal flag is clear on return and the interrupt flag as it was.
- **The W1 contract holds** (interface version 8): while OOUI draws, block 33 is in W1, so everything OOUI reads
  then (the widget table, the texts, the theme, the accelerator table, the event area, the picker's buffers) is in
  `$0300-$3FFF` or `$8000-$BFFC`, never `$4000-$7FFF`.
- **No call from a hook** (functions 80's frame or tick hook, 82's ES5503 hook). OOUI runs only in the program's
  own time.
- **No BDOS call** except in `ui_open`, `ui_close` and `ui_resume` (functions 80, 81 and 83: the interface, the
  mouse, the font, the pointer), the file picker's directory reads (functions 17, 18, 26, 32, 45, 49, 51 and 52),
  which run inside `ui_poll` a few at a time, `ui_pick`'s start (25, 32: the current drive and user), and (U2) the
  pointer set up inside `ui_poll` when a mouse is plugged in (81, 83).
- **No blocking call.** A message box or the picker is opened by one call and answered by an event from `ui_poll`,
  so an app that feeds a stream or runs a game keeps its frame.

### Handles ###

- A handle is a record's index in the app's table, **0-239** (`$00-$EF`).
- **`$F0-$FE` are OOUI's own** built-in dialogs: `UI_MSG` (`$F0`) the message box, `UI_PICKER` (`$F1`) the file
  picker; their widgets `$F2-$FD` (U2; `$FD`, the picker's "Add all", U3b); the rest reserved.
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
| +4 | `UW_W` | width in cells (a window's includes its frame); 0 fills to the parent's client edge, and a negative width fills to that many columns short of it |
| +5 | `UW_H` | height in cells; 0 fills, negative as the width |
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
| 4 | `UT_LABEL` | the text | (*U3b, proposed*) +10 its own attribute, 0 the theme's `TC_LABEL` | - | - |
| 5 | `UT_BUTTON` | the text | a toggle's state, 0 or 1 | - | - |
| 6 | `UT_CHECK` | the text | 0 or 1 | - | - |
| 7 | `UT_GROUP` | the title (0: none) | the chosen radio button, 0 its first | - | - |
| 8 | `UT_RADIO` | the text (a group's child) | - | - | - |
| 9 | `UT_MENUBAR` | - | - | - | - |
| 10 | `UT_MENU` | the title (the menu bar's child) | - | - | - |
| 11 | `UT_ITEM` | the text (a menu's child) | a check mark, 0 or 1 | +12 a key's HID usage shown, +13 its modifiers (`UK_*`) | - |
| 12 | `UT_SEP` | - (a menu's line) | - | - | - |
| 13 | `UT_VSCROLL` | - (*U3b, proposed:* with `UF_SPRITE`, +9 the thumb's sprite, and with `UF_OWNTHUMB` +8 its size byte) | the position, 0 to `UW_A` | the last position | the page (the thumb's size) |
| 14 | `UT_HSCROLL` | the same | | | |
| 15 | `UT_LIST` | the rows: an array of string pointers, or with `UF_ROUTINE` the row routine | the selection, `$FFFF` none | the rows' count | the top row shown |
| 16 | `UT_FIELD` | the buffer (0-terminated) | the cursor | +12 the buffer's size with its 0, +13 the first character shown | - |
| 17 | `UT_VBAR` | - | the level, 0 to `UW_A` | the top | - |
| 18 | `UT_HBAR` | the same | | | |
| 19 | `UT_VSLIDER` | - (*U3b, proposed:* with `UF_OWNTHUMB`, +8 the thumb's size byte) | the value, 0 to `UW_A` | the top | +14 a key's step, +15 the thumb's sprite (`UF_SPRITE`) |
| 20 | `UT_HSLIDER` | the same | | | |
| 21 | `UT_CANVAS` | - (*U3b, proposed:* with `UF_ROUTINE`, the routine of its cells) | - | - | - |
| 22 | `UT_CHOICE` (U2, proposed) | an array of `UW_A` string pointers | the one shown | the count | - |

**The flags** (`UW_FLAGS`):

| Bit | Common | Window | Panel, group | Label | Button | List | Field | Slider |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 `$01` | `UF_HIDDEN` | | | | | | | |
| 1 `$02` | `UF_DISABLED`: drawn dim, takes no input | | | | | | | |
| 2 `$04` | `UF_DIRTY`: OOUI's, set by the calls that change a widget | | | | | | | |
| 3 `$08` | `UF_NOFOCUS`: Tab passes it by | | | | | | | |
| 4 `$10` | | `UF_CLOSE` close box | `UF_FRAME` | `UF_CENTRE` | `UF_DEFAULT` (Enter) | `UF_MULTI` marks | `UF_UPPER` folds to capitals | `UF_SPRITE` thumb (*U3b:* a scroll bar's too) |
| 5 `$20` | | `UF_MODAL` | | `UF_RIGHT` | `UF_CANCEL` (Escape) | `UF_SCROLL` its own scroll bar | | `UF_OWNTHUMB` (*U3b:* with `UF_SPRITE`, the program's image; a scroll bar's too) |
| 6 `$40` | | `UF_OVERLAY` on the overlay layer | | | `UF_TOGGLE` | `UF_ROUTINE` rows (*U3b:* a canvas's cells too) | | |
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
record and defines `id` as its handle (`.assert`ing that the parent came first); `UI_BEGIN` and `UI_END` put the
table in `DATA`, since OOUI writes its records' flags and state; a text is a string literal, which the macro puts in
`RODATA` with its 0, or an address (0: none).

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
UI_CANVAS   id, parent, x, y, w, h, flags [, routine]       ; (U3b) the routine with UF_ROUTINE
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
| +10 | `UO_OVER` | the layer for menus, dialogs and `UF_OVERLAY` windows: `$FF`, none (they go on the base layer and what they covered is drawn again when they close), or the layer above the base (1, with `UO_BASE` 0), so the overlay is on top; anything else is `UX_LAYER` (U1 review, O10) |
| +11 | `UO_SPRITE` | the first sprite OOUI may use (the pointer, then slider thumbs) |
| +12 | `UO_SPRITES` | how many, 0-16 (0: no pointer sprite) |
| +13 | `UO_FLAGS` | `UO_OWNMOUSE` `$01`: the app sets the mouse's mode and the pointer itself |
| +14-15 | `UO_WIDGETS` | (U2, proposed) the widget modules' records the table uses, a list ended by 0: `UI_END`'s `name_WIDGETS`; 0 none (a table of U1's types only). *Ignored from U3a: every module is in OOUI.BNK* |

**Before `ui_open`** the app calls OOLIB's `GX_INIT` with the same event area, then `GX_PALINIT` and `GX_SPRINIT`:
the palette and the sprite table are OOLIB's, shared with the app, and OOUI can't tell whether they were made (a
program's BSS isn't cleared), so it never makes them itself.

It checks OODOS's interface (function 80's `FT_INFO`: version 8 or more, with `OA_INPUT` and `OA_DMA`, as `GX_INIT`
does) and the area (layout 2); takes its card memory (`GX_VALLOC`); makes its font; sets the layers it was given;
puts the theme's colours in OOLIB's palette shadow (`GX_PALSET`, then `GX_PALSHOW`); sets the mouse to absolute mode
with limits 319 and 239 (function 81's `IN_MOUSE`) and the pointer on `UO_SPRITE` (function 83's `DM_POINTER`),
unless `UO_OWNMOUSE`; checks the table; marks every visible widget dirty; and gives the focus to the first that
takes it. Its maps are cleared (`GX_VFILL`) before the layers show them, so nothing from before shows (U1 review,
O15). Nothing is drawn until `ui_poll`. While OOUI is open, `ui_open` is refused (`UX_BUSY`) and changes nothing;
`ui_close` when it isn't does nothing (O9). Errors: `UX_VERSION`, `UX_AREA`, `UX_LAYER`, `UX_MEMORY`, `UX_TABLE`,
`UX_BUSY`, `UX_SPRITE` (U2). *Proposed (the U2 review, X11-X12):* each widget's value is held to its range as the
table is checked, as `ui_setvalue` would hold it; and a slider's `UF_SPRITE` thumb must be one of OOUI's sprites after
the pointer's (`UO_SPRITE` + 1 to `UO_SPRITE` + `UO_SPRITES` - 1), or `ui_open` refuses the table with `UX_SPRITE`.

**The table's rules** (`UX_TABLE`): a parent before its child; an unused record (`UT_NONE`) is skipped whatever
its parent, so a zeroed record is good, but is no one's parent; a menu's parent a menu bar, an item's or a line's
a menu; and no record more than 16 deep (`UI_DEPTH`: its parents and itself) (O6, O14).

`ui_close` frees OOUI's card memory (`GX_VFREE`), turns the pointer off and shows the console's screen
(`GX_CONSOLE`); the sliders' sprite thumbs go out of sight first, their images pointed off the memory it frees (U2
review, X3). The warm boot does the same for a program that ends without it: the program's card memory is no
one's after it, and the console's screen comes back. The event area stays the app's to close.

### The calls ###

| Call | In | Out, and what it does |
| --- | --- | --- |
| `ui_open` | A/X the block above | carry clear; or carry set, A `UX_*` |
| `ui_close` | | |
| `ui_poll` | | A the event (`UE_*`), X its widget, Y as the event says, and `UI_EV` (below). Carry clear |
| `ui_show` | A a handle | shown with its children; a `UF_MODAL` window takes the input and the focus |
| `ui_hide` | A a handle | hidden with its children; what it covered drawn again. `UI_PICKER` or `UI_MSG` closes it, with no event (the picker's listing stops). A widget in it held by the pointer is let go: a slider's or a scroll bar's last `UE_CHANGE` comes at the next poll (U2 review, X10) |
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
| `ui_theme` | A/X a theme (0: the default) | colours and metrics changed; everything drawn again. While OOUI is shut or suspended, only kept: nothing on the card is touched, and `ui_open` or `ui_resume` sets it |
| `ui_msgbox` | A/X a block (below) | carry set (`UX_BUSY`) if one is up |
| `ui_pick` | A/X a block (below) | carry set (`UX_BUSY`) if the picker is up |
| `ui_relist` | | the picker's directory read again (after the app mounted a drive) |
| `ui_suspend` | | OOUI stops drawing; `ui_poll` returns every key, button and wheel event as `UE_RAW`; the layers are the app's. The sliders' sprite thumbs go out of sight; a key repeating, a first click and a press held are dropped (U2 review, X1, X3, X9, X10) |
| `ui_resume` | | the layers and the pointer set again; everything drawn again |

Errors (`UX_*`, *provisional numbers*): `UX_VERSION` 1 (OODOS's interface below 8, or no input or DMA), `UX_AREA` 2
(no event area, or layout 1), `UX_MEMORY` 3 (no video memory), `UX_TABLE` 4 (an unknown type, a parent after its
child, a menu item outside a menu, more than 240 records), `UX_HANDLE` 5 (a handle past the table, or a call the
type doesn't take), `UX_BUSY` 6 (`ui_open` while open; a box or the picker up), `UX_LAYER` 7 (`UO_BASE` past 1,
or `UO_OVER` neither `$FF` nor above the base), `UX_PICK` 8 (U2: `ui_pick` given no work buffer or no room),
`UX_SPRITE` 9 (U2 review, *proposed*: `ui_open`, a slider's thumb sprite the pointer's or not OOUI's).

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
| 11 | `UE_PICK` | `UI_PICKER` | 0 | 0 chosen (the picker closed; *U3b, proposed:* with `UP_KEEP` its OK is "Add", and the picker stays open with its marks), 1 cancelled (closed), 2 its extra button (still open), 3 (*U3b, proposed:* `UP_ALL`'s "Add all") every file listed given, as many as the result buffer holds (still open) |
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
| +4 | `UP_MULTI` `$01` several files; `UP_NEW` `$02` a name typed that isn't there (saving); `UP_DRIVES` `$04` drive buttons; `UP_USERS` `$08` user numbers 0-15; *(U3b, proposed)* `UP_KEEP` `$10` OK is "Add" and the picker stays up; `UP_ALL` `$20` an "Add all" button |
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

The picker reads the directory with functions 17 and 18 in idle polls, a few matches a `ui_poll` (four, and half
as many more as it has listed, at most 64), so listing never takes long. OODOS keeps one search, which the app may
use between polls, so each poll's part walks the directory from its start (function 17 with the picker's own FCB),
passes the names listed already and lists the next few (U2 review, Y1): an app's search between polls neither ends
the picker's nor is taken over by it, and a directory that changed between polls is read as it is now. A directory
that hasn't changed comes back in the same order, so each match is compared with the listed entry where it should
be, and the whole list is looked through only when they differ; past the list's end with every match in its place,
a match is new (round 2, Z1). A walk that reaches the directory's end takes out the files it no longer found, and
the selection moves with them: up one for each file gone before it, none if its own file went, and then the name field that showed it is cleared, unless `UP_NEW` made the field the user's (Z5; round 3, W1). An app's own
search must end before it polls while the picker is up, since the picker's walk starts a search of its own. It lists
each file once, however many extents it has. It names the drive in its FCB, so the current drive never changes,
and it sets the user (function 32), the DMA address and segment (26, 51) and the error mode (45) for each part,
after reading the app's (32, 52, and the SCB's `$4B` by function 49), and puts the app's back after it. The answer
is `UE_PICK`.

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
| +63 | `TH_POINTER` | the pointer's colours (U1): its outline in bits 7-4, its fill in 3-0 |

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

**Glyphs.** OOUI copies the console's font (card `$000400`, `VM_FONT`, OODOS's part: a card-to-card copy's source
may be anywhere in card memory, so `GX_VCOPY` copies it in one call) and redefines these codes (*provisional*; `UG_*` in ooui.inc), keeping CP437's box drawing (`$B3-$DA`), shades
(`$B0-$B2`), blocks (`$DB-$DF`) and arrows (`$10`, `$11`, `$18-$1B`, `$1D-$1F`; `$1C`, CP437's right angle, is
`UG_HBAR7`):

| Codes | Redefined as |
| --- | --- |
| `$01-$0F` | vertical bars filled 1/16 to 15/16 from the bottom (16/16 is `$DB`) |
| `$12-$17`, `$1C` | horizontal bars filled 1/8 to 7/8 from the left (8/8 is `$DB`) |
| `$7F`, `$9B` | a check box empty, ticked |
| `$9C`, `$9D` | a radio button off, on |
| `$9E`, `$9F` | a folder, a drive (the picker) |

So a vertical bar or slider *n* cells long draws 16*n* levels, and a horizontal one 8*n*. CP437's accented letters
(`$80-$9A`, `$A0-$A8`) stay, for names that have them.

**Card memory.** One piece of 16 KB, a card bank (`GX_VALLOC` 64 card pages, aligned to 64), so that one
`GX_VMAP` (aperture slot 1, block 33 in W1) reaches all of it and OOUI draws by writing cells there:

| Offset | Size | What |
| --- | --- | --- |
| `$0000` | 5 KB | the base layer's map (4,800 bytes) |
| `$1400` | 5 KB | the overlay layer's map (not used when `UO_OVER` is `$FF`) |
| `$2800` | 2 KB | sprite images: the pointer and slider thumbs, 16×16 at 4 bits, a card page (256 bytes) each, since a sprite's image address counts card pages: 8 of them |
| `$3000` | 4 KB | the font |

Each part starts on a card page, which the base registers count (`GX_LAYER` takes the map's page in `GX_V`, the
font's in `GX_R`; `GX_SPRIMG` an image's page). The palette and the sprite table are OOLIB's (`GX_PALINIT`,
`GX_SPRINIT`, which the app calls before `ui_open`), shared with the app; OOUI writes only its group's entries and
its sprites.

**What OOUI uses of OOLIB** (`oolib.inc`, `vmem.inc`, interface version 8; no function 84 is needed, as
[oolib.md](oolib.md) says):

1. **Allocate:** `GX_VALLOC`, `GX_V` 64 card pages, A 64 (the alignment): A/X the first card page, a card bank's
   first; `GE_MEMORY` becomes `UX_MEMORY`.
2. **Free:** `GX_VFREE`, the same page and size.
3. **Reach it:** `GX_VMAP` the first page: slot 1 on that bank and block 33 in W1, A/X the CPU address (`$4000`);
   `GX_W1BACK` puts block 1 back.
4. **Copy:** `GX_VCOPY` (card to card: the console's font), `GX_VFILL` (the maps cleared); the cells, glyphs and
   pointer image are written through W1.
5. **Point the card at it:** `GX_LAYER` (CONFIG 0, text 80×30; `GX_V` the map's page, `GX_R` the font's),
      `GX_PALOFS` (the layers' groups), `GX_SHOW`, `GX_SPRIMG`, `GX_SPRATTR`, `GX_SPRSHOW`, and `GX_PALSET` and
   `GX_PALSHOW` for the theme's colours.

**The groups** ([question 7](#open-questions), answered: OOLIB's `GX_PALOFS`, added for OOUI). The base layer's
PALOFS is `TH_GROUP`, and `TH_PALETTE`'s 16 colours (the CGA's for 0) go in its entries (*group* × 16 on); the
overlay's is `TH_OVERGROUP`, with `TH_OVERPAL`'s colours (`TH_PALETTE`'s for 0); and the pointer's sprite takes
the overlay's group too. With `TF_DIM`, a dim group of its own and an overlay layer, `TH_DIMGROUP` gets the base's
colours each component halved, and the base layer is put on it while a modal window *on the overlay* shows (the
dialog stays bright). A modal window on the base layer dims nothing, since it would dim itself; with no overlay
layer (`UO_OVER` `$FF`) nothing is dimmed and the dim group's entries aren't written. A theme in groups other than 0 keeps out of the program's own entries; colour 0 is still
transparent, and where both layers are, the palette's entry 0 shows, whatever the groups.

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
    about 4 KB of code for U1's core and text backend, about 3 KB more for U2. *Measured at U1 (2026-11-11, after
  its review):* 7.8 KB of code, 0.6 KB of tables and 236 bytes of `BSS` ([U1](#u1-the-core-and-the-text-backend)).
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
- **Memory:** 120 KB for a bitmap 512 screen, of the 1.5 MB of card memory a program has on revision 2.

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

- **Golden frames** (U1). A test program (`oodos/test/ui/uitest.s`) opens OOUI on scenes, and a host suite
  (`emulator/test/host/test_oodos_ui.cpp`) drives it with the USB card's keyboard and mouse, hashes the frame the
  card shows (FNV-1a over `VideoCard::frame()`'s 640×480 pixels) and compares it with the hash stored in
  `emulator/test/host/golden/ooui.txt`. A hash, not a picture: a line a frame in the repo, no image library, and
  the emulator's frames are exact. The hashes are made by the suite from OOUI's own drawing, never from a fixture,
  and only on purpose: `OOUI_GOLDEN=update ctest --preset fast -R test_oodos_ui`, after looking at the frames
  (`OOUI_DUMP=dir` writes each as a PPM there). A frame that differs is written as `ooui_NAME.ppm` beside the
  test. A change that is meant to change the look (a theme's colour, a glyph) regenerates them in the same
  commit, which says so.
- **Input scripts.** Keys and the mouse come from the USB card's host side (`UsbCard::key`, `mouse_to`,
  `mouse_buttons`), as `test_oodos_input` drives them.
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
| U0 | API spec: widget records, events, themes, file picker; checked against OO-Amp and both backends | API decided, open questions settled: **met** (the owner, 2026-11-11) |
| U1 | Core and text backend: desktop, windows, labels, buttons, menus, focus, themes, glyphs, layer 1 overlays, pointer hit-testing | Golden frames pass: **built** (2026-11-11), [below](#u1-the-core-and-the-text-backend) |
| U2 | Scroll bars, list boxes, message boxes, text fields, sliders, bars, canvas; file picker on OODOS directory calls | File picker works on real disks: **built** (2026-11-11), [below](#u2-the-remaining-widgets-the-message-box-and-the-picker) |
| U3 | U3a: OOUI as a banked library ([proposed](#u3a-ooui-as-a-banked-library)); U3b: OO-Amp rebuilt on OOUI; gaps fixed in OOUI, not the app; before and after code size compared | OO-Amp clearly smaller; decide on the bitmap backend |
| U4 | Bitmap backend (optional), drawing assist if repaints are slow | Repaints fast enough to feel native |
| U5 | Programmer's Guide chapter, examples, docs/README.md phases, a second app | A second app built on OOUI |


### U1: the core and the text backend ###

**Proposed, built 2026-11-11** on video card revision 2 and OOLIB's interface 8.

**Where it is.** `oodos/include/ooui.inc` (the API: the names above, the layout macros); `oodos/ui/`, built by
`make -C oodos ooui` into `build/ooui.lib` through `libzp.py`'s check; a program in `OOUI_USERS` is linked with
`ooui.lib`, then `oolib.lib`, through the rules gate. The modules, and what each costs (code, tables, `BSS`, in
bytes):

| Module | What | Code | Tables | BSS |
| --- | --- | --- | --- | --- |
| `uicore` | `ui_open`, `ui_close`, the calls that change a widget, places (`RECT`), focus, damage | 1,930 | 14 | 96 |
| `zpuicore` | `UIP`, `UIQ`, `UIR`, `UIT`: zero page | | | 8 (zero page) |
| `uidraw` | the text backend: card memory, font and glyphs, layers, colours and groups, pointer, painting | 2,732 | 270 | 71 |
| `uimenu` | menu bar, drop-downs, menu keys and clicks | 1,912 | 26 | 43 |
| `uipoll` | `ui_poll`: the queue, keys, focus moves, accelerators, the mouse | 1,404 | 54 | 26 |
| `uitheme` | `UI_HOUSE`, the default: OO-Amp's 16 colours from its skin sheet | | 96 | |
| `uiturbo` | `UI_TURBO`: Turbo Vision's blue (entry 13 an opaque black, colour 0 being transparent) | | 96 | |

So 7.8 KB of code (7.1 KB before the review's two rounds of fixes and the groups) against the 4 KB target: the menus (1.9 KB)
and the damage-box painting are most of the difference. `uiturbo` is linked only by a program that names `UI_TURBO`; the rest come together, since `uicore`
draws and polls through the others.

**What it does.** Types 1-6 and 9-12 (desktop, window, panel, label, button, check box; menu bar, menu, item,
line); `ui_open` refuses the others (`UX_TABLE`) until U2 builds them, and the group and radio buttons go with
them. Every call in the table but `ui_setrange`, `ui_msgbox`, `ui_pick` and `ui_relist` (U2's). Painting keeps a
damage box for each layer: a dirty widget is drawn whole and its rectangle (with its shadow) added to its layer's
box; any other widget that overlaps the box is drawn again clipped to it, after the box is cleared to transparent.
So a window hidden, or a menu put up, leaves the screen as it was, which the tests check frame for frame. With
`UO_OVER` `$FF` the overlay's widgets are drawn on the base layer after the rest, with the same box.

**Choices U1 made** (small, inside the API):

- A button's focus is its colour (`TC_BUTTONFOCUS`) and a ► and ◄ at its ends; a window's close box is `[■]` from
  its frame's third column; a menu's check mark is `√`; a frame's style per `TH_FRAMES`, CP437's single and
  double lines, or blocks for `TS_HEAVY`.
- The menu bar's titles start a column in, `" title "` each with `TM_MENUGAP` between; a drop-down is under its
  title, moved left to stay on the screen, as wide as its widest item's text and, two columns on, its key's name
  (`Ctrl+`, `Shift+`, `Alt+`, then a letter, a digit or F1-F12: other keys show none). F10 puts the first menu
  down, as Alt and a title's letter put that menu down.
- The focus takes Space and Enter first, so Enter on a check box flips it, and the default button is for Enter
  when the focus is on something that doesn't take it.
- A key on its own that is a modifier (usages `$E0-$E7`) is taken and dropped, not `UE_KEY`.
- A click is where the pointer is when `ui_poll` reads it: the queue's button events carry no position. An app
  that polls each frame never sees the difference.
- The palette: the theme's groups as above. The pointer is 16×16 at 4 bits on sprite `UO_SPRITE`, depth 3, in the
  theme's `TH_POINTER` colours, in the overlay's group; `ui_close` and `ui_suspend` put its depth to 0 (O17).
- Sizes never wrap (O7): a fill, or a negative size, that comes to less than nothing is 0, and a size past the
  parent's client area is held to it; a frame's client area is never less than 0 wide or high; a widget of no
  cells draws nothing and isn't hit.
- A menu that is down goes up when a modal window outside it shows (`ui_show`, or `ui_flags` making one modal),
  and when `ui_flags` disables it; an item shown or hidden while its menu is down redraws the menu whole.

**Changed in the API text** (marked where they are): `ui_open` needs `GX_INIT`, `GX_PALINIT` and `GX_SPRINIT`
called first (a program's `BSS` isn't cleared, so OOUI can't tell); interface 8, not 4; a negative width or height
fills to that many cells short of the edge; the layout macros put the table in `DATA`; a theme's +63 is
`TH_POINTER`.

**Left for U2** (all done in [U2](#u2-the-remaining-widgets-the-message-box-and-the-picker)): key repeat (no U1 widget takes a held key), the double-click, `US_HOT` and the pointer's hover,
`UE_MODAL` (a modal window takes the input and the focus in U1, and dims the base with `TF_DIM`, but the event
comes with the message box), drawing clipped to the parent's client area (U1 clips to the screen and the damage
box only; so a child that sticks out of its parent stays drawn when the parent is hidden, O16), a mouse plugged in
after `ui_open` (OOUI sets the pointer up only at `ui_open` and `ui_resume`: an app that sees `EK_DEVICE` bring a
mouse calls `ui_suspend` and `ui_resume` meanwhile, O13), and the widgets of U2's row in the plan.

**Testing.** `test_oodos_ui` (27 cases) through `UITEST`, as above in [Emulator and testing](#emulator-and-testing).
Eleven golden frames: `desktop`, `window`, `menu-file`, `menu-select`, `focus-ok`, `turbo-window`, `turbo-menu`,
`pointer-press`, `pointer-hit`, `overlay`, and `dim-modal` (the review's: the base on its dim group under a modal
window). With `OOUI_GOLDEN=update` the suite writes the hashes, prints so loudly, and exits 77, which CTest reports
as skipped, never passed (O18); if anything else failed, it exits with the failure count instead (W7). Besides them, every event `ui_poll` returns for the keys and clicks
(the menus by keys, by Alt and a letter, by an item's letter, by the accelerator table; Tab and Shift+Tab; Space,
Enter, Escape and a button's letter; toggles; the close box; a click off a menu; the wheel), `ui_rect`'s places,
the table's refusals, and frames that must equal others: a menu put up, a window hidden and shown again, a label's
text changed and back, the theme changed and back, and the one-layer mode against two layers. A theme colour
changed (`TC_FRAME`) fails seven of the ten frames; the damage box not cleared fails seven of the equalities.

**The U1 review (2026-11-11)** found these, fixed on this branch, each with a case in `test_oodos_ui` that fails
without its fix: a change to a menu's item or a menu landed on its bar's record (O1); hiding a menu's bar left
its drop-down up (O2); a hidden item kept its row (O3); a disabled menu opened, and its items could be chosen (O4);
the accelerator table and a menu's item reached past a modal window (O5); unused records were refused where they
should pass, and accepted as a parent (O6); sizes could wrap round 256 (O7); `ui_open` could open over an open
session (O9) or on layers the wrong way round (O10); `ui_flags` left its old place and the focus stale (O11); a
letter didn't press a button that takes no focus (O12); a table deeper than `UI_DEPTH` opened (O14); the maps
showed what was in card memory until the first `ui_poll` (O15); the pointer stayed on after `ui_close` and
`ui_suspend` (O17). And the link-time W1 check below (O8).

**Its second round (2026-11-11)**, fixed the same way: `ui_theme` set the layers while suspended, or shut, on
memory no longer OOUI's (W1); an item hidden or shown while its menu was down didn't redraw the menu (W2); a modal
window on the base layer dimmed itself (W3); a framed window of no width gave its client area 254 columns (W4); a
menu stayed down under a new modal window, or when disabled (W5); `w1.py` passed a map with no segments in it
(W6); an update run that failed something still exited 77 (W7); a one-layer app's dim group was written (W8); an
item could be chosen by its key from a bar that doesn't show (W10).

**What it found in OOLIB**, and the owner's answers (2026-11-11), made on this branch:

- **`gxpal.s` brought 3.6 KB of `BSS`** with `GX_PALSET`: `GX_FADE`'s accumulators shared its module, so a program
  that set one colour linked them. `GX_FADE` is now `lib/gxfade.s` with its zero page in `zpgxfade.s`
  (`GXFADE_ZP`, was `zpgxpal.s`'s `GXPAL_ZP`). `UITEST`'s `BSS` went from 4,615 bytes (ending at `$3FF3`) to 1,597
  (ending at `$34F9`, with a 64-byte theme of its own added), so its event area and buffers could come back below
  `$4000`; `GXTEST`, which calls `GX_FADE`, keeps its 6,160.
- **No call set a layer's PALOFS:** `GX_PALOFS` (`lib/gxmode.s`; [oolib.md](oolib.md), "Video card revision 2").
- **The W1 contract is checked when a program is linked** (O8): `tools/w1.py` reads the map of every program in
    `OOLIB_USERS` and `OOUI_USERS` and fails the build if any of its segments overlaps `$4000-$7FFF`, or if the map
  has no segments to check (`test/check_tools.py` makes one that crosses, for OOLIB and for OOUI, and sees the build
  stop, and a doctored map fail). It doesn't see absolute equates (an event area at a fixed address isn't a
  segment), nor a `.PRL` overlay linked with the library, which has no address until function 59 relocates it: the
  program keeps those out of `$4000-$7FFF` itself.

### U2: the remaining widgets, the message box and the picker ###

**Proposed, built 2026-11-11.**

**Each widget a module of its own,** so ld65 links only what a program uses. A type past U1's has a module record
(`WR_*` in `ui/ui.inc`: its type, `WF_FOCUS`, `WF_RANGE`, `WF_LIST`, `WF_REPEAT`, and its handlers: draw, key,
the button down, the pointer moved while held, the button up, the wheel). The layout macros mark the modules a
table uses and `UI_END` lists their records as `name_WIDGETS`; the program gives the list to `ui_open` at
`UO_WIDGETS` (+14), which registers them, and refuses a table with a type whose module isn't there (`UX_TABLE`).
The message box and the picker register what they use themselves. *(U3a, proposed, built: every module is in
OOUI.BNK's block B, whose dispatcher finds a type's record itself; `UO_WIDGETS` is ignored, `UI_END`'s
`name_WIDGETS` is 0, and nothing is registered or refused for want of a module:
[U3a, as built](#u3a-as-built).)* Their widgets are records of OOUI's own after
the program's table (`uidlg.s`), given by public handles: `UI_MSG` `$F0` and its text `$F2` and buttons `$F3`
and `$F4`; `UI_PICKER` `$F1`, its drive `$F5`, user `$F6`, list `$F7`, name `$F8`, OK `$F9`, Cancel `$FA`, extra
`$FB` and status `$FC`; *(U3b, proposed)* and "Add all" `$FD`.

**Sizes** (bytes; all `RODATA` tables included in "tables"):

| Module | What | Code | Tables | BSS | OO-Amp code it would replace |
| --- | --- | --- | --- | --- | --- |
| `uicore` | (U1's, grown by U2: registry, dialog handles, clipping, ranges, `ui_setrange`) | 2,705 | 50 | 155 | |
| `uipoll` | (U1's, grown: the widgets' mouse and keys, drag, double-click, hover, repeat, `UE_MODAL`, the dialogs' filters) | 2,577 | 62 | 62 | `oa_ui.s`'s input and keys |
| `uidraw` | (U1's, grown: the widgets' drawing, parent clipping, a label's lines) | 2,869 | 270 | 76 | |
| `uimenu` | (U1's, unchanged) | 1,912 | 26 | 43 | |
| `uigroup` | group, radio buttons | 446 | 28 | 4 | the visualizer's and repeat's modes, where radio-like |
| `uichoice` | a value from a short list (Left and Right) | 252 | 14 | 2 | Settings' lines; the picker's drive and user |
| `uiscroll` | scroll bars | 838 | 32 | 14 | the playlist's scroll bar (`oa_list.s`) |
| `uilist` | list boxes, arrays or a row routine, marks, its own scroll bar | 1,501 | 14 | 25 | the playlist's drawing and keys (`oa_list.s`), the dialog's list |
| `uifield` | a text field | 512 | 14 | 7 | the Load Files name |
| `uibar` | bars | 265 | 35 | 6 | the VU and level meters |
| `uislider` | sliders, a sprite thumb | 1,445 | 60 | 15 | seek, volume, balance, the EQ's 11 bands (`oa_ui.s`'s sliders and their sprites) |
| `uicanvas` | the canvas (`UE_CANVAS`) | 71 | 14 | 0 | the spectrum's and the time's click zones |
| `uimath` | the scaling the bars, scroll bars and sliders share | 182 | 0 | 16 | |
| `uidlg` | the dialogs' 13 records | 85 | 0 | 209 | |
| `uimsg` | `ui_msgbox` | 561 | 31 | 26 | CONFIRM QUIT, the error boxes (`gui/ooadlg.s`'s) |
| `uipick` | `ui_pick`, `ui_relist` | 2,416 | 499 | 321 | Load Files (`gui/ooadlg.s`, `oa_list.s`'s directory reading) |

Zero page: still OOUI's 8 bytes (`zpuicore`), none in any U2 module, so an overlay is given its caller's as
before. **U2's code is 10,659 bytes against the ~3 KB target** (9,594 before the U2 review's fixes; 10,634 after
round 1; 10,581 after round 2, which made the picker's records from a table of templates, 144 bytes of tables for
about 320 of code; round 3's W1 took 78): 2,085 the core's growth, 5,512 the nine widget modules, 3,062 the dialogs. A program links the core (10,063 bytes of
code with U1's) and only the modules it uses: OO-Amp's port would take the list, scroll bar, slider, bar, canvas,
choice, message box and picker, about 7.5 KB more. That is past what one region outside W1 holds with OOLIB's graphics (16 KB at `$8000-$BFFC`, or 15.6 KB at
`$0300-$3FFF`): the test programs had to be four (`UITEST`, `UITWID`, `UITBAR`, `UITPICK`, all from
`oodos/test/ui/uitest.s`), each linked with `test/ui/uitest.cfg`, which puts OOUI's and OOLIB's code at `$8000`
and the test's own code, tables, data and BSS below `$1C00`, `$1C00-$2FFF` a 256-file work buffer for the picker
(`OOUI_CFG` in `oodos/Makefile`). That link is the
tests' only: a real program can't reproduce it, since a `.COM`'s image is one block from `$0300` with its BSS after
it, and one that ran to `$BFFC` would leave the program no room (U2 review, Y7). `UITPICK` has 8 bytes of
`$8000-$BFFC` left. So **U3 must take the shared banked block** (risks
below): the core and the modules in a block of their own reached by FARCALL, the program keeping its 16 KB.

**What each widget does** (as its module's header says): a group frames its radio buttons, `UW_VALUE` the one
chosen; Space, Enter, a letter or a click chooses, Up and Down move it, `UE_CHANGE` on the group. A choice shows
"◄ text ►"; Right, Up and Space the next (Space round the list), Left and Down the one before, a click on either
half. A scroll bar's arrows step it, its track pages it, its thumb drags (`UV_LIVE`, then a last `UE_CHANGE`), the
wheel moves it; it takes no focus, and the accelerator table's `UA_UP` and `UA_DOWN` step it. A list: Up, Down,
PgUp, PgDn, Home, End, Enter (`UE_SELECT`), Space marks a `UF_MULTI` one (`UE_MARK`); a click selects, a
double-click chooses, a click on a `UF_MULTI` one marks; its scroll bar pages; the wheel scrolls; rows from an
array or a routine (styles `UL_*`), a TAB right-aligning the rest. A field: printable keys in at the cursor
(`UF_UPPER` capitals), Left, Right, Home, End, Backspace, Delete; each change `UE_CHANGE` with `UV_LIVE`, a last
without it at Enter or when the focus leaves (`US_EDIT`). A bar: 16 levels a cell up, 8 across. A slider: Up and
Right up a step (`UW_B`'s low byte), Down and Left down, PgUp and PgDn an eighth, Home the top, End 0, the wheel,
a click or drag where the pointer is; with `UF_SPRITE` its thumb is an 8 × 8 sprite (`UW_B`'s high byte) placed
to the pointer unit after each paint that drew, shown only where its cell shows (inside its parents' client areas,
with nothing in front of it, under no menu that is down: U2 review, X4), and put out of sight when the slider is
hidden, OOUI is suspended or closed. A canvas leaves its cells transparent and
reports `UE_CANVAS` (`UC_PRESS`, `UC_MOVE`, `UC_RELEASE`, `UC_WHEEL`, `UV_X` and `UV_Y` in pointer units).

**The message box** (`ui_msgbox`): a modal window on the overlay, centred, its text's lines (CR, up to 4), its
buttons (OK; OK and Cancel; Yes and No); `UM_DEFAULT`'s has the focus; Enter presses the focus's, Escape the
last; Y and N. `UE_CLOSE` X `UI_MSG`, 0 OK or Yes, 1 Cancel or No; then it goes. `UX_BUSY` while one is up, and
then the live box is left as it was.

**The picker** (`ui_pick`, `ui_relist`): a modal window on the overlay with the drive (a choice: A:-G: and M:,
`UP_DRIVES`), the user (0-15, `UP_USERS`), the list (16 rows, its scroll bar, a column routine's text after a
TAB, `UF_MULTI` with `UP_MULTI`), the status ("Reading...", "*n* files", "No files", "Drive error", and "*n* of
*t* files" when fewer are listed than found, the work buffer too small: U2 review, Y6, Z5), the name (14 characters,
"*d*:" and a whole 8.3 name: Z4; typed with `UP_NEW`, else showing the selection), OK, Cancel and the extra button. It reads the directory in an idle part of `ui_poll`, a walk
from the directory's start each time that passes what is listed and lists a few more (four, and half as many as are
listed, at most 64: Y1), by functions 17 and 18 with the drive in the FCB and extent 0 (each file once), with the
program's user, DMA address and segment, and error mode kept and put back around each part (32, 52, 49; then 32, 26,
51, 45), errors returned (45 `$FF`) so a drive with no medium says so and doesn't end the program. A name typed
with `UP_NEW` must be one file's: a name of 1-8 and a type of 0-3, no wildcard, blank or delimiter that OODOS's
parser stops at, and "*d*:" before it names the drive (A: to P:); anything else is refused ("Not a file name",
the picker stays up: Y3). Types are filtered by `UP_TYPES`. It covers every drive OODOS reads: the SD drives (`oodos-8m`,
`oodos-32m`, and an `apple-do` image mounted as one), M:, and a drive with nothing mounted (an error). A DOS 3.3
disk is APPLE2's and DOS33's, not a CP/M drive, so not the picker's. The answer is `UE_PICK` X `UI_PICKER`: 0
chosen (the marked files, else the selected one, else with `UP_NEW` the name typed, in the result buffer, the count
at `UP_CHOSEN`), 1 cancelled, 2 the extra button (still up), and *(U3b, proposed)* 0 with `UP_KEEP` leaving the
picker up with its marks (OK is "Add"), 3 `UP_ALL`'s "Add all" (every file listed, as many as the result buffer
holds; still up); `UX_BUSY` while one is up, `UX_PICK` with no work
buffer or no room. Each poll's part is a search of its own, so a program's search between polls doesn't disturb
the listing (Y1); `ui_hide(UI_PICKER)` stops it (Y2); `ui_relist` reads the drive again.

**"Left for U2," done:** key repeat (a module with `WF_REPEAT`: the keys it takes but Enter, Escape, Tab and Space,
held, after `TH_TIMES`' delay at its rate, from `EV_KEYS` and `EV_FRAMES`, so the program polls each frame); the
double-click (`UV_DOUBLE`, the same widget within `TH_TIMES` + 2); `US_HOT` on the widget under the pointer (the
state only: no theme colour for it yet); `UE_MODAL` when the first modal window comes up and the last goes (X it,
`UV_VALUE` 1 or 0); clipping to the parents' client areas (so nothing sticks out of a hidden parent, O16; a menu's
drop-down leaves its bar); a mouse plugged in after `ui_open` gets the pointer at its `EK_DEVICE` (O13: a BDOS call
inside `ui_poll`, then, which the conventions now allow). A label's text is split at CR into its rows.

**The named gaps:** a choice widget is built (`UT_CHOICE`): OO-Amp's Settings need a value from a few texts changed
by Left and Right, and labels and accelerators would leave each program to build it again. The picker keeps its
marks across the extra button's presses (it stays up, its marks with it), which is OO-Amp's "kept marked across
ADD". ADD ALL (every file listed) is the picker's to offer, by the owner's rule that gaps are fixed in OOUI, not
the app: it is left for U3, to be shaped by OO-Amp's port (a flag for a second extra button, or marking all
with a key), and noted there.

**API changes from the text above** (marked where they are): `UO_WIDGETS` at +14 (was reserved); `UT_CHOICE` (22);
`UX_PICK` (8); `UX_SPRITE` (9, the review's X11); the picker's internal handles `$F2-$FC` named; a label's CR lines; `UE_MODAL` now sent, so a U1
test's expectations gained it where a modal window comes and goes.

**Testing.** `test_oodos_ui`, 38 cases at U2's build. Eleven U2 cases through `UITWID`, `UITBAR` and `UITPICK`: every new event
and key; the gate, the picker on A: (`oodos-8m`, 25 files: more than a screen), an empty B:, an `apple-do` C:, an
unmounted D:, M: with 40, user 3, a 32 MB `oodos-32m` drive on a card of its own, types, marks, the extra button,
a typed name, `ui_relist`; ten golden frames more: `u2-lists`, `u2-lists-turbo`, `u2-list-end`, `u2-field`,
`u2-levels`, `u2-levels-turbo`, `u2-msg`, `u2-msg-turbo`, `u2-pick`, `u2-pick-turbo`. A house theme colour
changed (`TC_LIST`) fails the four frames with a house list. The U2 review's fixes added 19 cases (57 then), `y1`
to `y6` and `x1` to `x13`, one a finding, each failing without its fix (`y4`'s and `y5`'s, which guard what was
right, fail with the restore or the extent broken); no golden frame changed. Round 2 added `z1`, `z2`, `z4` and `z5`
(61 now) and `y4`'s segment (Y4b); `z1` bounds the instructions a poll runs in the walk's code over 200 files, about
54,000 where the round-1 walk ran 1.27 million. `u2-pick` changed: the name field two columns wider (Z4). Round 3
added the two `w1` cases: a file before the selection deleted mid-listing, and the selected file itself; round 4
the two `v1` cases (65 now), two files gone in one walk (both before the selection; one before it and the selected
one), each place compared after the pruning so far (V1); no golden frame changed.

**The U2 review's round 2** (*proposed*, the owner's approval, 2026-11-11): Z1, Z4 and Z5 as above; a paint with
damage but nothing drawn (a window hidden over nothing) places the sliders' thumbs too (Z2). Left for U3a: a test of
a menu over a thumb, the pointer's image off the freed memory at `ui_close`, the cost of placing the thumbs after
each paint (Z3), and a walk that gets back in step after a file is added to the directory mid-listing (W2: today, once a
match is out of place, each new file past the list's end in that walk is looked for through the whole list).

**The U2 review's fixes** (*proposed*, the owner's approval of all 21, 2026-11-11), besides the picker's and the
errors above: a first click is forgotten after the double-click's time (a 16-bit stamp), and a press on another
widget, any key, `ui_open` and `ui_suspend` end it (X1); a drag past either end of a track is held to it before
it is scaled, and the scaling holds at `$FFFF` (X2); a part of a widget clipped away by its parents takes no
pointer (X5); a list that shrinks moves its top row up so its rows show (X6); a row's right part as wide as the
list isn't shown, and the line is drawn whole (X7); a press on the desktop, a panel or a label holds nothing and
draws nothing (X8); a key repeating stops when the focus moves or a modal window comes or goes (X9); the wheel at
a slider's end makes no event (X13).

### U3a: OOUI as a banked library ###

**Proposed (phase 1, 2026-11-11), for the owner's approval before anything is built.** The owner decided (U3,
2026-11-11) that OOUI's core and widgets live in banked memory that a program reaches by FARCALL, not in its image.
OO-Amp has 389 bytes of W0 free, and its port would link about 17.7 KB of OOUI's code. **The owner approved this
design with all seven recommended answers (2026-11-11).** A design review then found eight problems (D1-D8). They
are fixed below, each marked where it is answered. D5 reopened answer 3, and the owner then chose three blocks
([the blocks](#u3a-the-blocks), [question 3](#u3a-questions)). A second round found four more (E1-E4), also fixed
below. The design needs no change to OODOS or OOLIB; the
questions say where it leans on them.

**What there is.**

- **`OOUI.BNK`,** a file holding the code for three blocks (question 3), built from `oodos/ui/*.s`.
- **A small program part,** which a program links from `build/ooui.lib`. It holds:
  - the calls' stubs;
  - the loader, `ui_load`;
  - the variables OOUI shares, `UISTATE`;
  - OOUI's zero page;
  - copies of the two themes, each linked only when the program names it.

**Who loads it, and where it lives.**

- **The program loads it, by `ui_load`,** after `GX_INIT` and before `ui_open`. `ui_load` takes A/X: an FCB with
  its drive and name set, or 0 for `A:OOUI.BNK`.
  - **The default name is opened in user 0** (D8). `ui_load` reads the program's user (function 32), sets user 0
    for the open and the loads, and puts the program's user back before it returns, on every path. It doesn't
    rely on the file's SYS attribute (hw/oodos.md's Open, which reaches user 0 from another user only for a file
    with t2' set).
  - **An FCB the program gives** is opened in the program's current user.
- **The loads are all by function 83's `DM_LOAD`,** to physical addresses. That covers the header, the images and
  the fixups. The fixups go a record at a time to the buffer inside `UISTATE`, whose physical address comes from
  function 73. So the program's DMA address and segment are never changed (D8).
- **The steps, in order:**
  1. Record 0, the header, is read and checked.
  2. One block is allocated for each block in the file, by function 55, anywhere in banks 1-5 (blocks 4-23). The
     blocks are the program's, like any it allocates, and the warm boot frees them.
  3. `GX_REMAP` puts the blocks in the W1 map.
  4. Each block's image goes to the block's `$0800-$37FF`, which is `$C800-$F7FF` when the block is in W3.
  5. Each block is put in W1 through OOLIB (`GX_BLOCK`). Common memory is copied from `$F800-$FFFF` to the
     block's top 2 KB (`$7800-$7FFF` in W1), as FARCALL needs. The block's fixups are applied.
  6. Block 1 goes back in W1 (`GX_W1BACK`).
  7. **All of `UISTATE` is zeroed** (D2). The fixup buffer was in it, and nothing in it may start out as it was
     found: a program's `BSS` isn't cleared. `uopen` (today in `DATA` for that reason, `uicore.s`) and every other
     variable OOUI keeps there therefore start at 0. `uopen` is the only variable of OOUI's that relies on an
     initial value; the others are set before they are read, as in a linked build.
  8. The stubs are filled with FARCALL's address (function 72) and, for each call, the block and address from the
     header's entry table.
  
  The loader's code, its FCB and its buffer are all outside `$4000-$7FFF`.
- **Its refusals** (carry set, A the error):
  - `UX_LOAD` (10, new): no file, a short read, or a file that isn't an `OOUI.BNK`.
  - `UX_VERSION`: an interface older than the one the program was built for, or a file that needs more
    `UISTATE` or more imports than the program gives.
  - `UX_MEMORY`: no block free.
  - `UX_GX` (11, new): `GX_BLOCK` refused a block (`GE_MAP`, `gxmem.s`). That is what happens when `GX_INIT`
    wasn't called first, or refused (D8). The program must call `GX_INIT` first. OOLIB's state is in the
    program's uncleared `BSS`, so a `ui_load` before it is undefined; `UX_GX` is what OOLIB's own check catches.
  - `UX_STATE` (12, new): `UISTATE` not on a page (round 2, E2), checked first.
  - `UX_BUSY`: loaded already (a flag in the program part's `DATA`, not in `UISTATE`).
- **Cleanup on a refusal** (D8). On every refusal after the first allocation, `ui_load` frees each block it took
  (function 57), calls `GX_REMAP` again, puts block 1 back in W1 and puts the program's user back. The program is
  then as it was, but for its open file, which `ui_load` closes.
- **Each program loads its own copy.** The file is shared, not the memory, so nothing outlives the program. A load
  is done once and takes about 0.1-0.2 s.

**The file and how it is made** (D1, D3). `oodos/tools/mkbank.py` (new) makes `OOUI.BNK` from plain ld65 links in
binary format. **o65 isn't used.** ld65 2.19's o65 writer leaves out the low byte that the format requires after a
high-byte relocation (the review's probe, and one of this phase's: `lda #>(UISTATE+$1234)` gives the relocation
with no `$34`, and the stream after it can't be parsed either way). So none of its relocation kinds is relied on.

- **The header** (record 0) holds `'OUB'`, the format (1), the interface version (`UI_IFACE`, 1), the blocks (3),
  the `UISTATE` size it needs, the imports it expects (a count) and the entry table: for each call, in
  `ooui.inc`'s order, its block and its address. Then come each block's image (at most 12 KB, from `$C800`) and
  its fixups.
- **The links.** Each block is a link of its own. `UICOM`, the part every block holds (below), is a link of its
  own too:
  - `UICOM` is linked first, into one memory area from `$C800`.
  - Each block's link takes `UICOM`'s exports as absolute symbols. `mkbank.py` writes these from `UICOM`'s label
    file (ld65's `-Ln`). The block's code goes into one memory area, from `UICOM`'s end to `$F7FF`, whose size
    ld65 enforces.
  - `mkbank.py` copies `UICOM`'s image and fixups into every block's image.
  - The variables the blocks share are one module, `ui/uivars.s`, linked into every link. Its `BSS` alone is in
    a memory area at `__UIS__` (a `-D` value), so its layout is the same in each link.
  - OOUI's zero page is likewise at `__UIZ__`.
  - A block's private variables stay in its own area.
  
  Each link's map gives `mkbank.py` the block's bounds and sizes.
- **No call across blocks.** A block's link holds only its own objects, `UICOM`'s symbols and `uivars`. So a
  reference from one block to another's symbol, or from `UICOM` to any block's, is undefined, and ld65 stops the
  build. Calls across blocks go only through `UICOM`'s stubs, which far call a dispatcher at a block's first
  address after `UICOM`. That address is known from `UICOM`'s size alone, so no link depends on another
  block's link.
- **The fixups are found by difference and then proved** (D1; round 2, E1). `mkbank.py` links everything three
  times. Each kind of fixup moves by its own step between L0 and L1:

  | Kind | L0 | L1's step |
  | --- | --- | --- |
  | `UISTATE`'s page | page `$10` (`__UIS__` = `$1000`) | `+$0100`: the high byte +1, the low byte unchanged |
  | OOUI's zero page | `__UIZ__` = `$10` | `+5` |
  | a word import (an OOLIB routine or variable, FARCALL) | import *i* at `$4000` + *i* × `$400` | `+$0302`: the low byte +2, the high byte +3 (or +4 with a carry) |
  | a block number | 1, 2, 3 | `+$40` |

  - **L2, a check,** uses values unlike both: `UISTATE` at page `$87`, the zero page at `$2A`, the imports at
    irregular addresses that aren't spaced by pages or by any common step (`$5A3B`, `$6117`, `$4E85`, and so on;
    each a fixed constant in `mkbank.py`), and blocks 9, 13 and 17.
  - **Classifying the differences.** `mkbank.py` reads every place where L0 and L1 differ, in this order, and fails
    the build on any byte that is left over:
    1. **A word fixup:** two bytes whose 16-bit value is exactly `$0302` higher in L1. The import must be a real
       one: L0's value *v* must lie in some import's slot, `$4000` + *i* × `$400` to that plus `$3FF`, with *i* below
       the number of imports. That gives the import *i* and the addend, *v* − its base.
    2. **A page fixup:** a byte exactly 1 higher (round 3, F2: whatever its neighbours do, so `.byte UIP, >fbuf`
       and a table of `>a, >b` are taken).
    3. **A zero-page fixup:** a byte exactly 5 higher.
    4. **A block number:** a byte exactly `$40` higher, whose L0 value is 1, 2 or 3.
  - **Why nothing can be taken for something else.**
    - A lone byte can move only by 1, 5 or `$40`: none of these is a word's low step (+2), and none is its high
      step (+3, or +4 after a carry).
    - Two neighbouring lone fixups (a page byte, a zero-page byte or a block byte, in either order) make a 16-bit
      change whose high byte moves by 1, 5 or `$40`, and so is never `$0302`.
    - So a lone `#<` of an import moves by 2 and fits no kind; a lone `#>` (`#>GX_V`, `.byte >FARCALL`) moves by 3
      or 4 and fits no kind. Either is refused.
    - With slots `$400` apart and the addend held below `$400`, `GX_V+$100` still names `GX_V`, not the next
      import. An addend of `$400` or more is refused: no OOUI code uses one, since the variables are a few bytes
      each.
  - **The proof.** The table is applied to L0 with L2's values, and the result must equal L2 byte for byte, or the
    build fails. That proves the table complete for values the derivation never saw.
- **Page alignment** (D1; round 2, E2). `UISTATE` is page-aligned in the program, so a page fixup never carries.
  ld65 only warns when a segment can't be aligned (the review's probe links with a return code of 0). Two things
  catch it:
  - the oodos Makefile fails on any ld65 warning, and the program part's `uistate.s` holds a `.assert`
    `<UISTATE = 0`, which ld65 checks as an error when it links;
  - `ui_load` refuses, before it allocates anything, a `UISTATE` whose low byte isn't 0. It uses an error of its
    own, `UX_STATE` (12): `UX_VERSION` is for a file and a program that don't match, and this is the program's
    link alone.

  So a high byte never needs a carry. A low byte of
  a `UISTATE` address (`#<`) never changes and needs no fixup; a high byte (`#>`, as `uidlg.s`'s `lda #>UDLG`) is
  the page plus the offset's high byte, exact. An OOLIB import, or FARCALL, is taken only as a whole word (`JSR`,
  `STA` and the like). A `#<` or `#>` of one would differ by a single byte that fits no kind, and the build stops
  (no OOUI code does it today).

**The calls.** `ui_open` and the rest keep their names, registers, flags and results.

- **Each call is a 7-byte stub in the program:** `JSR FARCALL` / `.byte` block / `.word` address / `RTS`, which
  `ui_load` fills. Until then each stub is `LDA #UX_VERSION` / `SEC` / `RTS`, so a call made before loading is
  refused rather than going astray.
- **The conventions hold as written.** FARCALL carries A, X, Y and the flags both ways, so
  [the conventions](#conventions) are unchanged.
- **Versioning.** Calls are only ever added, at the end of the entry table, and keep their meanings. `UI_IFACE`
  goes up with each addition, and a program refuses a file older than the version it was built for.

#### U3a: the blocks ####

Each block has 12 KB of code window. `UICOM` sits at the same address in every block, from `$C800`.

**What `UICOM` holds** (D5). It is the closure of what the other blocks call from the core, worked out by a script
over the modules' listings. The script followed each routine's calls, jumps, data references and fall-throughs, in
`uicore`, `uidraw`, `uipoll`, `uimenu` and `uimath`, to the end (2026-11-11, at `bacdccd`).

- **The seeds:** the cell primitives (`GOTO`, `EMIT`, `REPT`, `PUTS`, `TEXTLINE`, `FILLR`, `BOXD`, `TATTR`,
  `NOACC`, `MAPW1`), the record helpers (`REC`, `RECT`, `DIRTY`, `PUBH`, `CLAMP`, `UPPER`), `SCALE`, and `ENTER`,
  `FRONT`, `CANFOCUS` and `SETFOCUS`.
- **What they bring:**
  - `TEXTLINE` brings `DLEN`; `BOXD` brings `BLINE`; `CLAMP` brings `WFLAGS` and `WREC`; `DIRTY` and `CANFOCUS`
    bring `FWIN` and `INSIDE`.
  - **`RECT` brings the most:** `CLIPIN`, `PLACE`, `SHRINK`, `AXIS` and `ROOM`, then, through `PLACE`, the menus'
    places (`MRECT`, `MWALK`, `MWIDTH`, `KID`, `REQ`, `ISMENU`, `QTEXT`, and `KEYTEXT` with `KCAT` and `KPUT`).
    That is 629 bytes of `uimenu`.
- **The total** is 2,604 bytes of code and tables, and 188 bytes of variables, which go to `UISTATE`.
- **Added to it:** the house theme (96), the strings that a record points to (about 150), and the stubs and
  dispatchers (about 100). **About 2,950 bytes in all.**

**Calls between blocks.**

- **Into block B, the widgets.** The widget modules' records stay in B. The core calls a handler through a stub
  that far calls B's dispatcher, with X the type and Y the handler. B fills a table of each type's `WF_*` flags in
  `UISTATE` at `ui_open`, the one thing the core read from a record. Every module is in the file, so `UO_WIDGETS`
  and `ADDW`'s registering go.
- **What is left crossing back** from the widgets to the core: nothing in a paint or a poll. `REFOCUS` and `HIDEH`
  are called only from the dialogs, when one opens or closes.
- **Into block C, the dialogs.**
  - **The row routine.** The picker's list's row routine, in block C, is reached from the list, in B, through a
    stub in `UICOM`: the picker puts that stub's address in the list's `UW_TEXT`.
  - **The event filters.** While a dialog is up, `ui_poll` hands each event to the dialog's filter first (`DFILT`,
    through `udf1` and `udf2`, which point into C): one crossing from A to C for each event (round 2, E3).
  - **The listing part,** in an idle poll.

**The split, from U2's measured sizes less `UICOM`'s closure** (estimates, bytes, of 12,288 each):

| Block | What | Its own | With `UICOM` | Free |
| --- | --- | --- | --- | --- |
| A | `uicore`, `uipoll`, `uidraw`, `uimenu` | 8,150 | 11,100 | 1,190 |
| B | the nine widget modules but `uimath`; their private variables (73) | 5,670 | 8,620 | 3,670 |
| C | `uimsg`, `uipick`, `uidlg`'s code; the picker's private variables (FCB, directory buffer: about 270) | 3,770 | 6,720 | 5,570 |

**Three blocks: the owner's answer (round 2, 2026-11-11).** With two, block B (the widgets and the dialogs) came to
12,410 bytes with `UICOM`, 122 over. Keeping `RECT`'s menu branch in A would have left it 508 bytes free, already
under this design's 512-byte trigger before U3's own additions.

**The program's data, and OOUI's.** W0-W2 stay mapped through a far call, so every address the program gives OOUI
is the same address inside the block. That covers the table, the texts, the theme, the accelerator table, the
event area, the picker's blocks and buffers, and the results. OOUI reads them as it does now, under the W1
contract: outside `$4000-$7FFF` while it draws. No pointer is translated. The danger runs the other way: an address
inside a block means nothing to the program or to another block. So:

- **`UISTATE` holds what the program reads and what more than one block uses.**
  - What the program reads: `UI_EV`, `UI_RECT` and `UI_RAW`, at `UISTATE`'s +0, +12 and +16, fixed for good.
  - What more than one block uses: `uicore`'s state with `uopen`, the dialogs' 13 records, the types' flags, the
    picker's row, name and status texts (drawn by B's list and field and A's label), and what `UICOM`'s code
    touches.
  - Its size is about 1,300 bytes with the picker's row buffer (E3, question 8; about 700 without it). It is
    reserved as 1,536, its size rounded up to a whole multiple of 256 (answer 5).
  - It is a page-aligned `BSS` segment (`align = $100`) in the program part's `uistate` module. `tools/w1.py`
    keeps it outside `$4000-$7FFF`, as it does any segment.
- **Strings and tables another block reads,** or whose address goes into a record (a dialog button's text, the
  house theme), are in `UICOM` or `UISTATE`.
- **What one block alone uses** stays in that block. The BDOS reaches a parameter block in the caller's W3 block
  through the window its entry saved (`PMAP`, as function 73 maps `$C800-$FFFF`). So the picker's FCB and directory
  buffer can stay in block C.

**Where `UISTATE` goes, and its padding** (D1). Placed after a program's `BSS` with `align = $100`, it costs up to
255 bytes of padding. That is too much to find in OO-Amp's 389 free bytes of W0 on top of its own 1,536 (OO-Amp's
own UI code, which the port removes, frees more, but the port must not depend on it). So:

- **The rule:** a program puts `UISTATE` in a memory area of its own that starts on a page, outside
  `$4000-$7FFF`. Then there is no padding at all.
- **For OO-Amp:** the first pages of `$8000-$BFFC` (`$8000-$85FF`), if its overlays and buffers leave them, so
  `UISTATE` takes nothing of W0. Otherwise the page below `$4000` that its `BSS` reaches. U3b settles which, from
  OO-Amp's map.
- `com.cfg` gains no area. A program that uses OOUI adds one to its own config, as `uitest.cfg` adds its own, and
  the test programs show how.

**Zero page.** OOUI's 8 bytes (`UIP`, `UIQ`, `UIR` and `UIT`) remain the program's: `zpuicore` is linked into the
program, and ld65 places it in `$00-$EF`. An overlay is given its caller's addresses, as now (`tools/libzp.py`).
The block's references to those bytes are zero-page fixups, so the block works at whatever addresses the program's
link chose. They still hold nothing between calls. The blocks have no zero page of their own (`mkbank.py` checks),
and nothing is in `$F0-$FF`.

**The imports**, which the program part gives in an import table, in a fixed order that only grows:

- `UISTATE`'s page;
- OOUI's zero page;
- the OOLIB routines OOUI calls (`GX_VALLOC`, `GX_VFREE`, `GX_VMAP`, `GX_VCOPY`, `GX_VFILL`, `GX_LAYER`,
  `GX_PALOFS`, `GX_SHOW`, `GX_SPRIMG`, `GX_SPRATTR`, `GX_SPRPOS`, `GX_SPRSHOW`, `GX_PALSET`, `GX_PALSHOW`,
  `GX_CONSOLE`) and the variables it sets (`GX_V`, `GX_R`, `GX_X`, `GX_Y`, `GX_A`, `GX_C`);
- FARCALL's address, and the blocks' numbers, which `ui_load` supplies.

There are at most about 3,600 sites (every reference to OOUI's variables and zero page today: an upper bound, since
private variables need none), at about 2 bytes each in the file. All are handled while the file loads, so a call
pays nothing for them.

**OOLIB and the BDOS from inside a block.**

- **OOLIB is the program's.** Its state (the allocation map, the palette and sprite shadows, the cached W1 block
  and slot bank) is shared with the app, so OOUI links no OOLIB of its own and calls the program's by `JSR`, at no
  extra cost.
  - The import table makes the program link `gxmem`, `gxmode`, `gxpal` and `gxspr` (1.5 KB of code and 1.3 KB of
    `BSS`), which a program that draws with OOLIB has anyway.
  - OOLIB maps W1 and slot 1 as its contract allows. W3 holds OOUI's block all the while, and W1 never touches it.
- **The BDOS** is `JSR $0203`, as now. The BDOS's entry saves W3 (`ST_W3`) and puts it back, so the BDOS returns
  into the block. The functions OOUI uses are unchanged.
- **An interrupt** that comes while a block is in W3 is answered from the block's copy of common memory, which
  therefore must be exact. It is: the loader copies it from the running OODOS.

**Callbacks.** A list's row routine (`UF_ROUTINE`) and the picker's column routine are the program's code, called
by `JSR` from block B (the row routine) or C (the column routine), with W3 still on that block.

- **Their rules are as before:** they live outside `$4000-$7FFF`, they may map W1 through OOLIB, they use the
  program's zero page but not OOUI's, and they call no `ui_*`.
- **They may call the BDOS and FARCALL.** Both come back to OOUI's block.
- **A routine in a block of the program's own** is reached through a routine in the program's TPA that far
  calls it.
- **Hooks:** OOUI calls nothing from a hook, and no hook calls OOUI.

**Overlays** (D6).

- **The core's stubs.** An overlay that calls `ui_*` uses the core program's stubs and `UI_EV`, at the caller's
  addresses, as it uses its zero page; `libzp.py`'s `names` grows to export them (answer 6). It never links stubs
  of its own, which `ui_load` wouldn't fill.
- **The core's OOLIB.** An overlay that calls `ui_*`, or draws on anything OOUI also uses, **draws through the core
  program's OOLIB.** That OOLIB's routines and variables are exported to the overlay the same way. Such an
  overlay is never linked with an OOLIB of its own (`OVL_LIB`): a second copy keeps its own `GXCUR`, `GXBANK`,
  `GX_UNITS` and `GX_W1MAP`, and would get out of step with the state OOUI's calls use (a card bank allocated twice,
  a cached W1 block that isn't the one mapped).
- **This is a U3b acceptance item.** No OO-Amp overlay that calls `ui_*` links `oolib.lib`, and a build check says
  so. OO-Amp's GUI overlays are U3b's to change.

**The 65C816 rules gate** (D7). `rules.py` run on the blocks as built would fail falsely, for two reasons:

- an unrelocated operand that holds only an offset into `UISTATE` or the zero page looks like an absolute
  operand in zero page or the stack page (rules 2 and 3);
- a `JSR` to an import, or to FARCALL, is a `JSR` outside the image (rule 4).

So phase 2 changes the tool, and runs it like this:

- `mkbank.py` makes a copy of each block relocated against a dummy import table, with `UISTATE` at `$8700`, the
  zero page at `$2A`, and the imports in `$0300-$BFFC`.
- `rules.py` runs on that copy, with `--load 0xC800` and `--code` naming each block's (and `UICOM`'s) code
  segments.
- Rule 4 gains `--import`, a list of addresses the image may call: the import table's routine targets and
  FARCALL's.

**Checked against the rules.**

- **The windows.** OOUI writes no window register. W3 changes only through FARCALL, and W1 only through OOLIB
  (`GX_BLOCK`, `GX_VMAP` and `GX_W1BACK`), under contract version 8, whose W1 map includes the program's
  allocated blocks.
- **Memory.** The blocks are allocated by function 55 and freed by the warm boot, or by `ui_load` itself on a
  refusal. A segment may reach banks 1-5, so `DM_LOAD` can load them.
- **Common memory** is read and copied, never written, as the Programmer's Guide says a program does for now:
  [chapter 5, "Far calls"](../docs/oodos-programmers-guide/05-memory-and-banking.md#far-calls-farcall-and-the-far-call-table).
  FARCALL writes its own operand in the caller's copy, as it always does.
- **No hardware of its own:** no interrupts, DMA channels or VIA registers.
- **The program's state** is left as found: the user is put back, and the DMA address and segment are never
  changed.

**What a call costs** (estimates; phase 2 measures them). FARCALL costs 281 cycles more than a `JSR` and its `RTS`
(`test_oodos_sys`), and a frame is 119,318 cycles at 7.15909 MHz. A crossing between blocks costs about 300 cycles:
the stub, FARCALL and the dispatcher.

| What | Crossings | Cycles added | Of a frame |
| --- | --- | --- | --- |
| A call from the program (its stub and FARCALL) | | about 290 | 0.24% |
| An event loop's frame with nothing waiting: one `ui_poll` | | about 290 | 0.24% |
| A frame with three events: four polls | | about 1,170 | 1% |
| A widget of B's drawn: its handler | 1 | about 300 | |
| OO-Amp's screen drawn whole: about 30 such widgets | 30 | about 9,000 | 7.5%, once |
| An event a widget of B's takes (a key, a press, a drag's move) | 1 | about 300 | 0.25% |
| The sliders' sprite thumbs placed after a paint that drew: one crossing for all of them (`FRONT` and `RECT` are in `UICOM`) | 1 | about 300 | 0.25% |
| Each event while a dialog is up: its filter in C (`DFILT`) | 1 | about 300 | 0.25% |
| `ui_open`: the types' flags from B | 1 | about 300 | once |
| A dialog opened or closed (`REFOCUS`, `HIDEH` back into A) | 2-3 | about 900 | once |
| OOLIB, the BDOS, callbacks, `UISTATE` and the zero page | 0 | none | |

**The picker's list** (round 2, E3). Its rows come from C's row routine, which B's list calls once for each row it
draws. The list is drawn whole far more often than "when it scrolls":

- **After every listing poll.** `COUNTED` (`uipick.s`) marks the list dirty after each idle poll that lists more
  files.
- **After every selection move.** That is Up, Down, a click or the wheel.

As built today, that costs:

| What | Crossings | Cycles added | Of a frame |
| --- | --- | --- | --- |
| An idle poll while the picker lists: its listing part, the list's paint in B (`WCALL`'s draw), then its 16 rows | 18 | about 5,400 | 4.5%, each frame till the listing ends |
| A selection move: `DFILT`, the list's key handler, its paint, then 16 rows | 19 | about 5,700 | 4.8% |

**The plan for phase 2: one crossing for the rows of a draw.**

- **How.** When B's list draws a list whose row routine is the picker's stub, it makes one call into C, which fills
  the text and style of every row shown (16 at most) into a row buffer in `UISTATE`. The list then draws those
  rows from the buffer. The public row routine (`UF_ROUTINE` in a program's table) is unchanged: it is the
  program's code, called by `JSR` with no crossing.
- **The revised cost:**

  | What | Crossings | Cycles added | Of a frame |
  | --- | --- | --- | --- |
  | An idle poll while the picker lists: the listing part, the paint, the rows | 3 | about 900 | 0.75% |
  | A selection move: `DFILT`, the key, the paint, the rows | 4 | about 1,200 | 1% |

- **Its price.** The buffer is 16 rows of 40 bytes, which replaces the picker's one 40-byte row and is about 600
  bytes more of `UISTATE`. That moves `UISTATE` from about 700 to about 1,300 bytes, reserved as 1,536 (answer 5),
  and the program part from about 1.5 KB to about 2.3 KB (question 8).

**Sizes.** The program part is about 2.3 KB, against about 17.7 KB of OOUI's code and 1.3 KB of tables that
OO-Amp's port would link today:

| Part | Bytes (estimate) |
| --- | --- |
| The stubs (22 calls, `DATA`) | 154 |
| `ui_load`, with its refusals' cleanup, the user switch and the zeroing | 480 |
| The import table | 50 |
| `UISTATE`, in a page-aligned area of its own, with the picker's row buffer (E3) | 1,536 |
| Zero page | 8 |
| `UI_HOUSE` and `UI_TURBO`, each only if named | 96 each |

`OOUI.BNK` is about 26 KB of images plus at most about 8 KB of fixups. Its three blocks take 48 KB of the
program's 320 KB.

**API changes** (all *proposed*):

- **New:** `ui_load`, `UX_LOAD` (10), `UX_GX` (11), `UX_STATE` (12) and `UI_IFACE`.
- **Every widget module is in the file** (round 2, E4), and that changes three things:
  - `UO_WIDGETS` is ignored, and `UI_END` sets `name_WIDGETS` to 0.
  - **`ui_open` no longer refuses a table** with a widget type whose module wasn't registered (`UX_TABLE`,
    `uicore.s`'s `CHECKTAB`, through `WREC`). Every type of U2's is always there; an unknown type is still
    refused.
  - **The modules' records and the `UIW_*` exports go.** The macros no longer import `UIW_*`, and a program that
    named one (none does but through `UI_END`) no longer links.
  
  Built: the U2 text above that describes the registry ("Each widget a module of its own") and `ooui.inc`'s
  `UO_WIDGETS` comment say so.
- **The themes** (D4). `UO_THEME` 0 is still the default, the block's own copy of the house theme. `UI_HOUSE` and
  `UI_TURBO` stay tables a program can link (`uitheme`, `uiturbo`: 96 bytes each, linked only when named), so a
  program can still pass the house theme by address or build a theme from it. `UITEST`'s `c_theme` copies
  `UI_HOUSE` this way. A theme a program passes is read where it lies, as now.

**The tests.**

- **The programs.** `UITEST`, `UITWID`, `UITBAR` and `UITPICK` become ordinary `.COM`s, linked with `cfg/com.cfg`
  plus a page-aligned area for `UISTATE`, `ooui.lib`'s program part and `oolib.lib`, as a real program is.
  `uitest.cfg` goes (answer 4). `test_oodos_ui` puts `build/OOUI.BNK` on M: (`put_m`), and each test program
  loads it with an FCB that names M:.
- **What must still hold.** All 65 cases and all 21 golden frames must pass unchanged through the blocks (with
  D4, `c_theme`'s `UI_HOUSE` copy unchanged too). The one exception is `u2-levels`, which X4's menu-over-thumb
  test changes (the decisions for U3a).
- **New cases:**
  - `ui_load`'s refusals: no file, a bad header, an older interface, too little `UISTATE`, no block free,
    `UX_GX`, and `UX_STATE`: a test program linked with `UISTATE` off a page (one byte in), refused before anything
    is allocated (E2). Each checks that the blocks it took are free again (a second `ui_load` succeeds, and function 53
    finds the memory), and that the user is put back;
  - `UISTATE` filled with `$FF` before `ui_load`, then `ui_open` succeeding and the frames unchanged (D2);
  - the default name opened from user 3 with `OOUI.BNK` in user 0 and its SYS attribute clear (D8);
  - the program's DMA address and segment the same after `ui_load` (D8);
  - the blocks freed by the warm boot;
  - the cycles of an idle `ui_poll` and of a crossing;
  - the picker's crossings counted in a listing poll and in a selection move: 3 and 4 (round 3, F1), with its rows filled in
    one crossing (E3).
- **Must-fails:**
  - the copy of common memory left out: the first call never comes back, and the case fails on its time limit;
  - a kind of fixup skipped: the frames differ;
  - in `test/check_tools.py`:
    - a test object in block A that does `JSR` to a block B symbol: ld65 stops the build (D3);
    - a `UICOM` routine that calls into A: the same;
    - a fixup table with one site dropped: `mkbank.py`'s L2 comparison fails (D1);
    - a `#<` and a `#>` of an OOLIB import, and a `.byte >FARCALL`: `mkbank.py` refuses each (D1, E1);
    - an addend of `$400` on an import: refused (E1);
    - `UISTATE` misaligned in a program's link: ld65's assertion stops the build (E2).
- **Phase 2's other items** (Z3, W2, the menu-over-thumb test, the pointer's image after `ui_close`) are built on
  top of this.

#### U3a: as built ####

**Proposed, built 2026-11-12** on `feat/ooui-u3a`, as the design above says but where this says otherwise.

**Where it is.**
- **The bank's modules** are `oodos/ui/*.s` but `uiturbo.s`, assembled with `UIBANK` defined. `uia.s`, `uib.s` and
  `uic.s` are the dispatchers. `uifar.s` holds UICOM's stubs, and `uirows.s` the picker's row buffer (E3).
- **`ui/uiimp.inc`** lists the calls and the imports, in order.
- **`cfg/ooui-bnk.cfg`** and **`tools/mkbank.py`** make `build/OOUI.BNK` (`make -C oodos ooui`), with
  `build/ooui-bnk.lbl` for the tests.
- **The program part** is `oodos/uiprog/`: `uistub.s`, `uiload.s`, `uistate.s`, and `zpuiload.s`, which is
  `zpuicore.s` again. It is built with `uitheme.s` and `uiturbo.s` into `build/ooui.lib`.
- **The test programs** link with `test/ui/uiprog.cfg`, a `.COM` with a UISTATE area at `$8000`. `uitest.cfg` is
  gone.

**How the blocks are linked: one link, not one a block.**
- An object can't be split across links, and UICOM's routines live in their modules' sources (they are wrapped in
  `.segment "UCODE"` where they are). So mkbank.py links every module once. UICOM, A, B and C are memory areas of
  their own, the three blocks all starting after UICOM at the same address, each written to its own file.
- **No call across blocks** is checked from L0's debug file, not by an undefined symbol. Every reference is a line
  in a segment, and the symbol it names is in a segment, so a reference from one block's segment to another
  block's symbol, or from UICOM's to any block's, fails the build. The dispatchers' entries, `AENT`, `BENT` and
  `CENT`, are allowed.
- **A segment that falls through into another** fails it too: the last instruction before a segment directive
  in a code segment must be RTS, RTI, JMP or BRA.
- **`check_tools.py` sees each refusal:** block A calling block B's `PNONEW`, UICOM calling `ui_open`, UICOM
  falling into A's code, a `#>` of an import, an addend of `$400` (the proof), and a table with one fixup left
  out (the proof).

**Crossings.**
- Each block's dispatcher takes a selector (`xsel`) to a table of its routines.
- Block B has every widget module's record. The core's `WCALL` crosses to B with the type and the handler.
  `ui_open` crosses once, for the types' flags (`ufl`).
- Calls into block C are the dialogs' filters (`DFILT`), their idle part (`IDLE`) and the row fill (E3).
- Calls into block A are `REFOCUS` and `HIDEH`, from the dialogs. So are the sliders' post-paint call (`upost`)
  and `THUMBSOFF`, which go A to B through `XB_POST`.
- **UICOM is the measured closure** (2,604 bytes then), with:
  - the tables its routines read: `INT2PUB`, `FRAMES`, the menus' key names;
  - the dialogs' strings a record points to;
  - the house theme;
  - the row routine `PROWS`;
  - the stubs.

**Sizes, measured** (bytes; a block's code window is 12,288):

| Part | Bytes | Free |
| --- | --- | --- |
| UICOM, in every block | 3,283 | |
| Block A, with UICOM | 11,369 | 919 |
| Block B, with UICOM | 9,278 | 3,010 |
| Block C, with UICOM (and its own 164: the picker's FCB and directory buffer) | 7,005 | 5,283 |
| UISTATE, used of the 1,536 reserved | 1,531 | 5 |
| Fixups: A, B, C (UICOM's in each) | 1,846, 1,688, 1,132 | |
| `OOUI.BNK` | 37,248 | |
| The program part: `ui_load` (code 1,259, tables 56, `DATA` 4; with the code review's C2 checks) | 1,319 | |
| The program part: the stubs (`DATA`) | 154 | |
| The program part: zero page | 8 | |
| The program part: `UI_HOUSE`, `UI_TURBO`, each when named | 96 | |

- **UISTATE is full.** A variable more needs a whole page more of the reserve (answer 5), or a block's own
  variables moved out of it.
- **`ui_load` is larger than the 480 estimated.** Its refusals' cleanup and its fixup kinds took more. It runs
  once, so a program short of room can put it in an overlay.
- **The test programs** are now 4,818 to 6,621 bytes each (`UITEST` to `UITBAR`), where they had been about 26 KB
  linked at `$8000`.

**Cycles, measured** (`test_oodos_ui`; a frame is 119,318):

| What | Crossings | Cycles |
| --- | --- | --- |
| A crossing's overhead (FARCALL in, the dispatcher, FCRET out) | 1 | 322 |
| An idle `ui_poll` on UITEST's scene (31 records) | 0 | 20,433 (17%) |
| The sliders' thumbs after a band moved, on a screen shaped as OO-Amp's (Z3) | 0 | 69,591 (58%) |
| The sliders' thumbs after a status line's text changed far from them | 0 | 9,638 |
| A picker listing poll | 3 | |
| A picker selection move | 6 | |
| `ui_load` from M:, once | | 2,067,222 (0.29 s) |

- **The idle `ui_poll`** was 98,858 cycles, 83% of a frame, before the fix below; U1 and U2 had the same cost, and
  the banking adds only the call's FARCALL.
- **The thumbs after a band moved** were 3,302,244 cycles, 28 frames, before Z3's fix.
- **The selection move's 6 crossings** are the list's key, the picker's filter of its `UE_CHANGE`, the picker's
  idle part, the list's paint, its rows, and the name field's paint. Round 3's F1 counted 4.

**Fixed on the way, besides the design** (each with a case that fails without it):
- **Z3.**
  - A sprite slider whose last thumb cell is outside the paint's damage (`pdmg`, kept from `udmg`) is left as it
    was.
  - `FRONT` skips the overlay's pass when no window can be on it.
  - `RECT` reuses its parent's placing for a sibling during a `FRONT`.
- **An idle paint.** `PASS` no longer places a clean record when its layer's damage box is empty (it would draw
  nothing). No golden frame changed.
- **W2.** The picker's compare place jumps only over a few entries (kept in `skipq`, 8 at most, to check past the
  list's end), stays put for a far find, and passes entries this walk has stamped. The worst poll's walk went from
  823,876 instructions to 57,906.
- **The pointer's image.** `ui_close` points the pointer's image at card page 0.
- **The fixups' count.** It was the sites, not the encoded entries (skips too), so ui_load stopped short of the
  table's end. Now it counts the encoded entries; every frame shows it.
- **The link's order.** It is sorted, since make's wildcard ordered `ui/*.s` differently in make_card.sh's
  container, and OOUI.BNK's bytes with it.

**The code review (2026-11-12), all six fixed:**
- **C1.** A thumb whose cover hid only it kept the slider's first cell as its last, so it stayed out of sight
  when the cover went. SPRITE now keeps the thumb's own cell, shown or not. The case
  `c1_a_thumb_shows_again_when_what_covered_only_it_goes` fails without the fix (depth 0 after the window hid).
  Its second half, a second menu of UITBAR's over the thumb only, passes either way, since the menu bar's redraw
  widens the damage box.
- **C2.** ui_load checks every header field and fixup before using it, and refuses a file that fails (UX_LOAD,
  through FREE):
  - before anything is allocated: each block's image 1 to `$3000` bytes, and each call's block 0-2 and its address
    in `$C800-$F7FF`;
  - for each fixup: inside the image, a word's second byte too; a kind mkbank.py makes; a block number 1-3; an
    import below the header's count.
  
  `c2_a_hostile_ooui_bnk_writes_nothing_outside_its_blocks` has seven files, each refused with the blocks free and
  the user and DMA as they were. The loader before accepted all seven.
- **C3.** mkbank.py refuses an unnamed label in the bank's sources: the debug file doesn't list one, so a `jmp :+`
  across blocks couldn't be seen. The mkbank.py before accepted check_tools' case.
- **C4.** The must-fails added:
  - in check_tools: a `#<` of an import, and a `.byte >FARCALL`;
  - `c4_a_short_ooui_bnk_is_refused_with_its_blocks_freed`: a file short in an image, and one short in the fixups.

  The two hand must-fails, run on 2026-11-12, each broke the window's case:
  - common memory not copied into the blocks: `a_window_with_labels_buttons_and_a_check_box` NOT OK, 12 failed
    assertions;
  - the page fixups skipped in ui_load: the same, 12 failed assertions.
- **C5.** The dispatchers' headers say that A, X, Y, the carry and V are kept across a crossing, and N and Z
  aren't.
- **C6.** `rules.py`'s rule 4 is given only the routine imports; `ui/uiimp.inc` marks the variables `UIIMPV`. A
  JSR to `GX_V` is refused (check_tools); the mkbank.py before let it through.

**Changes from the design text:**
- **The X4 menu-over-thumb test** adds a menu bar to UITBAR's scene that is hidden there and shown by the test, so
  `u2-levels` didn't change: no golden frame changed in U3a.
- **The tests load OOUI.BNK from M: in user 15**, out of the picker's listings of user 0. The four cases with 50
  files on M: load it from B:, since M: (120 KB) can't hold both.
- **`the_table_is_checked_and_handles_too`** has E4's change: a list in a table without its module registered is
  opened (and closed again before the case goes on), not refused.
- **The demo card has OOUI.BNK on A:** (user 0, not SYS), and test_oodos_boot's demo-card case wants it. The test
  card is unchanged.

#### U3b: the gaps OO-Amp's port found ####

**Proposed, built 2026-11-12** on `feat/ooui-u3b`, for OO-Amp's port (U3b), whose look the owner kept (2026-11-12,
option A: its pixel chrome on layer 0, OOUI's widgets and text on layer 1 over it with transparent backgrounds,
its dialogs OOUI's windows). Each is an addition: no U1, U2 or U3a call, record or event changes meaning.

- **A thumb of the program's own** (`UF_OWNTHUMB`, `$20`, with `UF_SPRITE`; a slider or a scroll bar). OOUI never
  writes the thumb's image (the program sets it with `GX_SPRIMG`); its size byte, `GX_SPRATTR`'s Y (height << 6,
  width << 4, the palette group; not 0), is the record's `UW_TEXT` low byte. Every sprite thumb is now centred by its
  size (half its width across a vertical track's column, half its height on a horizontal track's row), which for
  OOUI's own 8 x 8 is where it was. OO-Amp's thumbs are 16 x 8, 8 x 8 and 8 x 32.
- **A scroll bar's sprite thumb** (`UF_SPRITE` on `UT_VSCROLL` and `UT_HSCROLL`, the sprite `UW_TEXT`'s high byte).
  The bar then works in sprite units, not cells: the thumb is the sprite's length, at (track - thumb) x value / last
  along the track; a press on it drags it, before or after it pages; it is placed after a paint as a slider's is
  (`POST`, with its Z3 filter and X4's rules), and `ui_open`'s `UX_SPRITE` check covers it. OO-Amp: the playlist's
  scroll bar, an 8 x 32 thumb moved to the pixel.
- **A label's own colour:** `UW_VALUE`'s low byte, not 0, is the label's attribute (0: the theme's `TC_LABEL`, as
  before). OO-Amp's words are in a dozen colours of its skin; one role couldn't draw them. `UI_LABEL` takes it as an
  optional last argument.
- **`US_HOT`'s colour** (U3's decision): the theme's `TF_HOT` flag (`TH_FLAGS` `$04`). With it, a button under the
  pointer is drawn in `TC_BUTTONFOCUS`'s colours (without the focus's pointers) and drawn again as the pointer comes
  and goes. No new role: the theme stays 64 bytes. OO-Amp's theme leaves it clear (its buttons are its art).
- **The picker's Add and Add all** (U3's, shaped by the port): `UP_KEEP` (`$10`) makes OK "&Add": its `UE_PICK` 0
  gives the chosen files and leaves the picker up, with its marks; `UP_ALL` (`$20`) shows an "Add a&ll" button
  (public handle `$FD`): `UE_PICK` 3, every file listed given in the result buffer, as many as it holds, the picker
  up. The extra button stays the program's (OO-Amp's MOUNT). "Add all" is drawn leftmost but comes last in the Tab
  order, after the extra button, since its record is the dialogs' last (accepted, the review's G8). Its record is the dialogs' 14th: to keep `UISTATE`
  inside its 1,536 bytes, the picker's variables that only block C reads (its scratch record, the program's state
  it keeps, the SCB block, a row's text, the walk's skip list: 84 bytes) moved to block C's own memory. `UISTATE`
  uses 1,463 bytes now (1,531 before).
- **The program's own pointer:** `UO_OWNMOUSE` already did it (no change): OOUI sets no pointer and still takes the
  mouse's clicks from the event area. A case checks it.
- **A canvas whose cells are the program's** (the owner's Q1, 2026-11-12; additive). `UF_ROUTINE` (`$40`, a list's
  bit) on a `UT_CANVAS` makes `UW_TEXT` a row routine, called as a list's row routine is. OO-Amp's zone C needs it:
  its scope and VU modes colour each cell, on OOUI's layer.
  - **The call.** For each row it draws, top first, OOUI calls the routine with a `JSR` from block B, with OOUI.BNK
    in the bank's window, as a list's row routine is called: there is no crossing, and the routine is the program's
    own code, in its memory. A/X is the row (0 the canvas's first) and Y the canvas's handle.
  - **The answer.** A/X is the row's cells, as many as the canvas is wide (`ui_rect`'s width): each a character
    (code page 437) and an attribute, as a cell on the card is, in the layer's palette group, outside
    `$4000-$7FFF`. A/X 0 leaves the row transparent.
  - **What the routine may and may not do.** It may map W1 itself (OOUI maps its own again after each call). It may
    use the program's zero page, not OOUI's. It may not call `ui_*` or the BDOS. It returns with the decimal flag
    clear.
  - **Each frame.** OOUI draws the cells when the canvas is dirty or under the paint's damage, so the program calls
    `ui_dirty` on the canvas when they change (OO-Amp: each frame). The pointer on it is still `UE_CANVAS`. Without
    `UF_ROUTINE` a canvas is transparent, as before.
  - **The cost** (`u3b_a_canvas_s_cells_from_a_routine`, 40 x 4 cells in a window): 21,907 cycles to draw the canvas
    (18% of a frame), with the routine's four calls and its own `GX_VMAP`. A plain canvas's transparent fill costs
    about 14,700 in the same place. The whole `ui_poll` after `ui_dirty` takes 73,313 cycles, against 66,122 for a
    plain canvas. The rest is the paint around it, the same for either kind: the records walked, the damage box
    cleared, the window's inside drawn again under it, and the interrupts that fall in that time.

**Tests:** `test_oodos_ui` gains six `u3b_*` cases (a scene of UITBAR's, blocks 6 and 7, and the picker's block F):
five fail without their change; one confirms what `UO_OWNMOUSE` already did. One existing case changed how it
measures, not what it expects:
`what_a_call_and_a_crossing_cost` now takes the least of the crossings it times, not their mean, since an interrupt
inside one lengthens it by chance (a build whose code had moved measured 689 cycles, where the crossing is 322).
No golden frame changed.

**The code review, round 1 (2026-11-12):** the owner had G1-G7 fixed as recommended and G8 accepted.
- **G1.** `ui_close` points OOUI's own thumbs' images at card page 0, off the memory it frees, and leaves a
  `UF_OWNTHUMB` image as the program set it. `u3b_a_slider_s_own_thumb` closes OOUI and checks both kinds.
- **G2.** A sprite thumb shows only where its first and last cells along the track both show (held to the track),
  not its middle's alone, and the Z3 filter tests every cell from the first to the last. OO-Amp's 8 x 32 covers up
  to five cells. Two `FRONT`s cost the moved band on OO-Amp's screen 50,000 cycles more, so `SHOWS` walks the records
  once for both cells: 77,056 cycles, 5,600 more than before. `u3b_a_tall_thumb_covered_at_one_end_is_hidden`: a
  window over the thumb's last row, then a drop-down over its first.
- **G3.** `ui_settext` on a slider's or a scroll bar's `UW_TEXT` (the thumb's size byte, a scroll bar's sprite)
  checks the word as `ui_open` does, and refuses it with `UX_SPRITE`, the record unchanged. If it takes a new
  sprite, the old one goes out of sight. `SPRCHK` also refuses an own thumb's size byte of 0, in `ui_open` too.
  `u3b_ui_settext_on_a_thumb_is_checked`.
- **G4-G6.** The tables above (`UE_PICK` 0 with `UP_KEEP` and 3, the label's `UW_VALUE`, the thumbs' `UW_TEXT`, the
  flags, `$FD`), `ooui.inc`'s `UP_FLAGS`, and stale comments in the sources.
- **G7.** `what_a_call_and_a_crossing_cost` prints a crossing's mean and longest beside the least, which it still
  holds under 500 cycles.
- **G8.** "Add all" last in the Tab order: accepted, noted above.

**Sizes after the review's fixes and Q1's canvas** (mkbank.py; a block's code window is 12,288 bytes): `UICOM`
3,298; block A 11,603 (685 free; 817 before), B 10,485 (1,803 free; 2,396 before), C 7,222 (5,066 free, unchanged);
`UISTATE` 1,463 of 1,536, unchanged (G2's and the canvas's variables are block B's own).

#### U3a: questions ####

**The owner's answers (2026-11-11):** all seven as recommended. The D5 closure reopened answer 3, and the owner
chose three blocks (round 2). None of these needs an OODOS or OOLIB change.

1. **Common memory in OOUI's blocks:** rely on the interim copy. *Answered.*
2. **`OOUI.BNK` on the card:** in phase 2, a line in `make_card.sh` and the cards regenerated. *Answered.* The
   file's SYS attribute isn't needed, since `ui_load` opens it in user 0 (D8).
3. **Two blocks or three.** *Answered: three* (the owner, round 2, 2026-11-11): A, the core and the menus; B, the
   widgets; C, the dialogs. First answered two, then reopened by D5: the closure makes `UICOM` about 2,950 bytes,
   and two blocks no longer fit ([the blocks](#u3a-the-blocks)).
4. **`uitest.cfg`:** retired once `test_oodos_ui` runs only through the blocks. *Answered.*
5. **`UISTATE`'s reserve:** rounded up to a whole 256. *Answered.*
6. **Overlays:** `libzp.py` exports the core's stubs, and, with D6, the core's OOLIB, to an overlay. *Answered.*
7. **A lean crossing:** no; FARCALL only. *Answered.*
8. **The picker's row buffer** (round 2, E3). *Answered (the owner, round 3): the buffer, 1,536 bytes of `UISTATE` reserved.* Filling the shown rows in one crossing needs a 16-row buffer of about
   600 bytes more in `UISTATE`, which is in the program's memory. **Recommended:** the buffer, as planned above:
   it keeps the picker's code in C. **Alternative:** put the picker's row routine (`ROWFN`, 143 bytes, with what it
   calls) in `UICOM`. Then B's list calls it directly, with no crossing and no buffer, at the cost of about 150-300
   bytes in each of the three blocks (A has about 1,190 free).

## Risks ##

| Risk | Mitigation |
| --- | --- |
| Library grows too big to link into every app | ar65 links only used modules; move the core into a shared banked block or RSX if size bites |
| API leaks text-mode assumptions | Positions and sizes in abstract units; theme supplies metrics; review against both backends in U0 |
| Repaints are slow in bitmap mode | Dirty-widget redraw; the drawing assist as the fix |
| Apps still special-case UI | OO-Amp as the first port; add what it needed to OOUI rather than to the app |
| Text-mode look feels too plain | Theme work and custom glyphs first; bitmap backend if it's still not enough |


## Open Questions ##

**Recommended answers (U0, 2026-11-10); all six accepted by the owner (2026-11-11).** Each answer follows its
question.

1. Name: OOUI (proposed) or OOTK?
      **Accepted (2026-11-11):** OOUI, with the prefix `ui`, as the decisions above and the API use it.
2. Linked into each app, or a shared banked block or RSX from the start?
      **Accepted (2026-11-11):** linked into each app first, as OOLIB is and as the decisions above say; ar65
   links only the modules an app uses. A shared banked block (FARCALL) is reconsidered at U3's gate with OO-Amp's
   measured sizes, as the risks table says.
   **Decided (the owner, 2026-11-11):** the shared banked block from U3 on; U3a's design is
   [proposed below](#u3a-ooui-as-a-banked-library).
3. Which theme look: Turbo Vision blue, a grey Windows 3.1 feel, or an OO-1 house style?
      **Accepted (2026-11-11):** an OO-1 house style as the default theme, taken from OO-Amp's skin-sheet
   colours, so the proving app and the toolkit look like one machine; and a Turbo Vision blue theme beside it in
   U1, which costs one 64-byte table and proves that a theme restyles everything. The grey Windows 3.1 feel waits
   for the bitmap backend, where its bevels can be drawn.
4. Does OO-Amp's visualizer stay app-drawn on a canvas, or become an OOUI widget?
      **Accepted (2026-11-11):** it stays app-drawn on a canvas, as the text backend's canvas is meant for
   ("a visualizer or waveform at pixel resolution"); OOUI's bars cover simple meters such as a VU. Revisited only
   if a second app wants the same spectrum.
5. Is the bitmap backend wanted at all, or is a well-themed text UI enough for OO-1?
      **Accepted (2026-11-11):** decided at U3's gate, as the plan says, once OO-Amp runs on the text
   backend. The API is kept free of text-mode numbers ([Against both backends](#against-both-backends)), so
   nothing is lost by waiting.
6. Build the drawing assist only if the bitmap backend is built?
      **Accepted (2026-11-11):** yes: the text backend never needs it, so it is built only with the bitmap
      backend (U4), and only if that backend's repaints are measured too slow; with revision 2, on the video card.
7. A layer's PALOFS (raised 2026-11-11, fitting U0 to OOLIB): `GX_LAYER` set it to 0 and nothing set it
   otherwise, so OOUI couldn't put its layers on a theme's groups or dim the base under a modal window. Should
   OOLIB gain a call, or may OOUI write the register itself?
   **Answered (the owner, 2026-11-11):** OOLIB gains `GX_PALOFS` (A a layer, X the group), and OOUI uses it for
   `TH_GROUP`, `TH_OVERGROUP` and `TH_DIMGROUP` ([Text backend: resources](#text-backend-resources)).

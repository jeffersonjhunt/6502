# OOUI: the OO-1's UI toolkit #

Nov 10, 2026 · @Jefferson

**Status: proposed (2026-11-10); U1–U3b built and merged, in OODOS 0.9 (2026-11-13).** Phase U0 (below) settled the API and the open questions, as OOLIB's plan did.
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
then call `ui_dirty`. *(S1, proposed; the owner's rule:)* a field that moves or resizes a record (`UW_X`, `UW_Y`,
`UW_W`, `UW_H`, a window's frame), or its type or parent, needs `ui_redraw` after, not `ui_dirty`: what covers what
may have changed, and only `ui_redraw` says so (the sprite thumbs, the hit test). *(S2, proposed:)* OOUI keeps
each record's place in a cache; a record moved by its fields and only dirtied is caught at the next paint (its
place compared with the cache's), but a hit test before that paint still finds it where it was.

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
| 13 | `UT_VSCROLL` | - (*U3b, proposed:* with `UF_SPRITE`, +9 the thumb's sprite, and with `UF_OWNTHUMB` +8 its size byte) | the position, 0 to `UW_A` | the last position | the page (the thumb's size); *U3b, proposed:* with `UF_MARGIN`, +15 the margin byte and +14 the page, 0-255; with `UF_OWNTHUMB` and `UF_NOARROWS` too, the margin byte's high four bits the thumb's length ([an own thumb's length](#u3b-an-own-thumbs-length)) |
| 14 | `UT_HSCROLL` | the same | | | |
| 15 | `UT_LIST` | the rows: an array of string pointers, or with `UF_ROUTINE` the row routine | the selection, `$FFFF` none | the rows' count | the top row shown |
| 16 | `UT_FIELD` | the buffer (0-terminated) | the cursor | +12 the buffer's size with its 0, +13 the first character shown | - |
| 17 | `UT_VBAR` | - | the level, 0 to `UW_A` | the top | - |
| 18 | `UT_HBAR` | the same | | | |
| 19 | `UT_VSLIDER` | - (*U3b, proposed:* with `UF_OWNTHUMB`, +8 the thumb's size byte; with `UF_MARGIN`, +9 the margin byte) | the value, 0 to `UW_A` | the top | +14 a key's step, +15 the thumb's sprite (`UF_SPRITE`) |
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
| 6 `$40` | | `UF_OVERLAY` on the overlay layer | | | `UF_TOGGLE` | `UF_ROUTINE` rows (*U3b:* a canvas's cells too) | | `UF_MARGIN` (*U3b, proposed:* a press in its margin is its own; a scroll bar's too) |
| 7 `$80` | | `UF_NOFRAME` | | | | | | *(U3b round 5, proposed)* `UF_NOWHEEL` a slider's: the wheel over it is the program's (`UE_WHEEL`); `UF_NOARROWS` a scroll bar's: no arrows, the track the whole bar |

`UW_STATE` (OOUI's): `US_PRESSED` `$01`, `US_HOT` `$02` (the pointer is over it), `US_OPEN` `$04` (a menu down),
`US_FOCUS` `$08`, `US_DRAG` `$10`. *(S1, proposed:)* bit 7 (`US_THUMB`, `ui.inc`'s, not `ooui.inc`'s) is OOUI's
own: a sprite thumb to place at the next paint.

**A list's rows.** Without `UF_ROUTINE`, `UW_TEXT` points to an array of `UW_A` string pointers. With it,
`UW_TEXT` is a routine OOUI calls (`JSR`) for each row it draws: A/X the row, Y the list's handle; it returns A/X a
0-terminated string, in the program's memory, below `$4000` or in `$8000-$BFFF` (*U3b review round 2, H6*), and Y a style (`UL_NORMAL` 0, `UL_CURRENT` 1, `UL_DIM` 2, `UL_MARKED`
bit 7, which a style adds). The routine may map W1 itself (OOUI maps it again after the call), may use the app's
zero page but not OOUI's, and may not call `ui_*`. A multiple-selection list keeps its marks in the app: OOUI asks
for them through the style. *(U3b review round 2, proposed:)* OOUI clears the decimal flag after the call (H4), and a
routine of 0 calls nothing: every row empty, in `TC_LIST` (the selection's colour still shown) (H2).

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
UI_VSCROLL  id, parent, x, y, len, last, page, flags [, sprite, thumb, margin]   ; UI_HSCROLL the same; (U3b) the rest
UI_LIST     id, parent, x, y, w, h, rows, count, flags
UI_FIELD    id, parent, x, y, w, buffer, size, flags
UI_VBAR     id, parent, x, y, len, top, flags              ; UI_HBAR the same
UI_VSLIDER  id, parent, x, y, len, top, value, step, sprite, flags [, thumb, margin]   ; UI_HSLIDER the same; (U3b) the rest
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
O15). Nothing is drawn until `ui_poll`. *(U3b landing, the owner's decision 2026-11-13; a change to existing
behaviour:)* the layers are turned on (CTRL) at the end of the first `ui_poll`'s paint after `ui_open`, `ui_resume`
or `ui_theme`, not by these calls, so the first picture they show is the painted one; until then CTRL is as the
program left it. *(Review AA1:)* after `ui_open` and `ui_resume` the layers' own registers (map, font, palette
group) and the palette wait for that paint's end too, so a base layer the console shows (layer 0) keeps the
console's map and colours until OOUI's is painted. While OOUI is open, `ui_open` is refused (`UX_BUSY`) and changes nothing;
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
| `ui_flags` | Y a handle, A the bits to set, X the bits to clear | A the flags now; dirty. Not `UF_DIRTY` or `UF_HIDDEN` (`ui_show`, `ui_hide`). *(U3b review round 2, proposed:)* a slider's or a scroll bar's new flags are checked as `ui_open` checks the table: refused, carry set and A `UX_SPRITE`, the flags unchanged; `UF_SPRITE` cleared puts the thumb's sprite out of sight. *(Round 3, J2:)* refused the same way, a change to `UF_OWNTHUMB` while `UF_SPRITE` is on before and after the call; with `UF_SPRITE` turned on or off in the same call, it is taken. *(Round 4, K1:)* setting or clearing `UF_MARGIN` on a scroll bar changes what its `UW_B` means (with it, the page is the low byte and the margin the high; without it, the page is the whole word), so the program sets `UW_B` again first, for the meaning the new flags give it |
| `ui_settext` | Y a handle, A/X a string | dirty |
| `ui_setvalue` | Y a handle, A/X the value | held to the widget's range; dirty. No event |
| `ui_getvalue` | Y a handle | A/X the value |
| `ui_setrange` | Y a handle, A/X a list's count or a bar's, slider's or scroll bar's top | the value held to it; dirty |
| `ui_dirty` | A a handle | it and its children drawn again at the next idle `ui_poll`. *(U3b review round 5, L1, proposed:)* after writing a window's `UF_MODAL`, `UF_OVERLAY` or `UF_HIDDEN` itself, the program calls `ui_dirty` on that window (or sets them with `ui_flags`, `ui_show` or `ui_hide` instead), so that the modal window and the overlay are found again (B3); for those bits on a record other than a window, or a record's type or parent, `ui_redraw`. *(S1, proposed:)* after moving or resizing a record by writing its fields, `ui_redraw` too: `ui_dirty` draws it again where it now is, but doesn't say that what covers what changed |
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
| 9 | `UE_WHEEL` | under the pointer | 0 | wheel steps no list or scroll bar took, signed (*U3b round 5:* nor a slider with `UF_NOWHEEL`, which is then X) |
| 10 | `UE_CANVAS` | the canvas | 0 | the pointer on a canvas: `UV_KIND`, `UV_X`, `UV_Y` (wheel steps in `UV_VALUE`) |
| 11 | `UE_PICK` | `UI_PICKER` | 0 | 0 chosen (the picker closed; *U3b, proposed:* with `UP_KEEP` its OK is "Add", and the picker stays open with its marks), 1 cancelled (closed), 2 its extra button (still open), 3 (*U3b, proposed:* `UP_ALL`'s "Add all") every file listed given, as many as the result buffer holds (still open) |
| 12 | `UE_RAW` | `UI_NONE` | the kind (`EK_*`) | an event OOUI doesn't use (sound, controllers, devices), its 8 bytes in `UI_RAW` |
| 13 | `UE_MODAL` | the window | 0 | 1 the first modal window shown, 0 the last hidden: the app may dim its own layer |

`UI_EV` (12 bytes, `UV_*`): +0 `UV_CODE`, +1 `UV_HANDLE`, +2-3 `UV_VALUE`, +4-5 `UV_X` and +6 `UV_Y` (the
pointer, in pointer units from the widget's top left), +7 `UV_KIND` (a canvas's: `UC_PRESS` 1, `UC_RELEASE` 2,
`UC_MOVE` 3 while a button is held, `UC_WHEEL` 4), +8 `UV_MODS` (`EV_MODS`'s then), +9 `UV_USAGE`, +10 `UV_CHAR`
(ASCII, 0 none), +11 `UV_FLAGS` (`UV_LIVE` `$01`, `UV_DOUBLE` `$02`, `UV_KEYED` `$04`: by the keyboard). *(U3b landing, review GG3:)* `UV_USAGE` and `UV_MODS` hold the last key
OOUI read: for an event no key made (a field's last `UE_CHANGE` as `ui_hide` moves the focus away, a click's) they
still hold that earlier key, so a program acting on a key checks `UV_KEYED` too.

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
| +11-12 | a column routine, 0 none: A/X an entry, Y its row; it returns A/X a short string for the row's right, or 0 for "not yet" (asked again later), so a slow look inside a file can be spread over frames. *(U3b review round 3, proposed:)* OOUI clears the decimal flag after the call (J1) |
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
| U3 | U3a: OOUI as a banked library ([proposed](#u3a-ooui-as-a-banked-library)); U3b: OO-Amp rebuilt on OOUI; gaps fixed in OOUI, not the app; before and after code size compared (as information) | *(the owner, 2026-11-13)* OO-Amp runs on OOUI with every feature working, no unapproved change in behaviour, start-up and dialogs within an agreed time; the bitmap backend not now: **met once the landing is approved** ([the outcome](#u3b-the-outcome); the size gate, "OO-Amp clearly smaller", was the wrong one) |
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
| An idle `ui_poll` on UITEST's scene (31 records) | 0 | 20,433 (17%); 878 after U3b's B1 and B3 (below) |
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
    (code page 437) and an attribute, as a cell on the card is, in the layer's palette group, in the program's
    memory, below `$4000` or in `$8000-$BFFF`. A/X 0 leaves the row transparent. A routine of 0 (`UW_TEXT` 0 with
    `UF_ROUTINE`) calls nothing: every row is transparent, as a plain canvas's are; it isn't refused.
  - **What the routine may and may not do.** It may map W1 itself (OOUI maps its own again after each call). It may
    use the program's zero page, not OOUI's. It may not call `ui_*`, the BDOS or FARCALL. **That differs from a
    list's row routine,** which may call the BDOS and FARCALL (U3a's callbacks, above): the canvas's is called
    inside the paint's inner loop, once a row, and must be quick, and a canvas's cells are the program's to have
    ready before it calls `ui_dirty`. OOUI clears the decimal flag after each call, so a routine that leaves it set
    does no harm.
  - **When it is called.** For each row the paint draws, and only those: a row outside what the paint redraws (the
    damage box, inside the canvas's parents) is skipped, with no call and no `GX_VMAP`. Today a paint draws a canvas
    once, but OOUI doesn't promise it: the routine may be called more than once for a row in one paint, and for
    fewer rows than the canvas has (a window moved off two of its rows calls it for those two). Its answer must not
    depend on how often, or for which rows, it is called: it gives the row's cells as they are, each time it is
    asked.
  - **Each frame.** OOUI draws the cells when the canvas is dirty or under the paint's damage, so the program calls
    `ui_dirty` on the canvas when they change (OO-Amp: each frame). The pointer on it is still `UE_CANVAS`. Without
    `UF_ROUTINE` a canvas is transparent, as before.
  - **The cost** (`u3b_a_canvas_s_cells_from_a_routine`, 40 x 4 cells in a window): 21,907 cycles to draw the canvas
    (18% of a frame), with the routine's four calls and its own `GX_VMAP`. A plain canvas's transparent fill costs
    about 14,700 in the same place. The whole `ui_poll` after `ui_dirty` takes 73,313 cycles, against 66,122 for a
    plain canvas. The rest is the paint around it, the same for either kind: the records walked, the damage box
    cleared, the window's inside drawn again under it, and the interrupts that fall in that time.

- **A margin for sliders and scroll bars** (`UF_MARGIN`, `$40`; owner-approved 2026-11-12, after OO-Amp's port
  found OOUI's hit areas smaller than its own: a vertical slider's one column wide, so its EQ bands' targets 584
  pixels smaller than today's, the playlist's scroll bar 352, volume and balance 96 each). Opt-in, and additive:
  without the flag nothing changes.
  - **The margin byte.** A slider's is `UW_TEXT`'s high byte (+9, free: its low byte is the own thumb's size); a
    scroll bar's is `UW_B`'s high byte (+15; it has no free byte, so with `UF_MARGIN` its page is `UW_B`'s low byte,
    0-255). Bits 0-3: the cells across the track on each side. Bits 4-7: the cells beyond each end along it, a
    slider's only; a scroll bar's ends are its arrows, and these bits do nothing there. `UI_*SLIDER` and
    `UI_*SCROLL` take it as an optional last argument.
  - **What a press there does.** A press in the margin is a press on the widget, as on its track: the same
    `WR_DOWN` with the pointer where it is, so the same value as the track's cell in that column (a horizontal
    slider) or row (a vertical one), an end's value beyond the end, the same page or thumb drag on a scroll bar,
    and the drag that follows. Only the press: the wheel and `US_HOT` look at the widget's own cells.
  - **What it never takes.** `MARGIN` (`uipoll.s`, block A) runs after `HIT` on a press: a margin takes the press
    only where what `HIT` found is one of the widget's own parents (its window's or panel's inside, the desktop)
    or nothing. So it never takes a press from another widget, whether drawn in front of it (FRONT's order: a
    window over it, a later record) or behind it. Behind too, which is more than "in front" asks: OO-Amp's volume
    (row 12) and balance (row 13) are neighbours, and a margin across either would otherwise take the other's
    presses. Where two margins meet, the frontmost widget's (the table's later record) has the cell. A margin is
    inside the widget's parents' client areas and the modal window, as `HIT` is, and a hidden widget has none.
  - **What OO-Amp sets** (its `tools/mkgfx.py`, from `art/art.json`'s hit rectangles; a cell is 4 x 8 pixels):

    | Widget | Type | `UF_MARGIN` and the byte | Its target then (today's) |
    | --- | --- | --- | --- |
    | `eq0`-`eq10` | `UT_VSLIDER` | `UW_TEXT` = `$11` << 8 \| the size byte | 12 x 80 (12 x 70): one column each side, one row beyond each end; each band's columns start 1 pixel right of today's |
    | `vol`, `bal` | `UT_HSLIDER` | `UW_TEXT` = `$20` << 8 \| the size byte | 96 x 8 (92 x 8): two columns beyond each end |
    | `seek` | `UT_HSLIDER` | `UW_TEXT` = `$11` << 8 \| the size byte | 312 x 24 (312 x 12): its pixels 86-97 cover three rows |
    | `plscroll` | `UT_VSCROLL` | `UW_B` = `$01` << 8 \| the page (the rows shown, under 256); `$51` with its thumb's length ([below](#u3b-an-own-thumbs-length)) | 12 x 88 (8 x 88): one column each side |

    Each covers today's rectangle (the bands' but for its first pixel column). Where a margin meets another of
    OO-Amp's widgets, that widget keeps its cells: if the playlist's list reaches column 77, the scroll bar's
    left margin is the list's there. A label or a canvas in a margin keeps it too (it is a widget, not a
    parent); OO-Amp's generator should check none sits in one. (The seek bar's difference by this count is
    1,312 pixels, against the port's 780; the port's numbers may come from a later layout.)
  - **The cost:** 205 bytes of block A (`MARGIN`, `MTEST`), 11 of block B (the scroll bar's page byte); no
    `UISTATE` (it uses `fb`, `fh` and `t0`-`t2`); nothing on a poll without a press. A press walks the records
    once more, a type test each, and `RECT` only for a flagged slider or scroll bar.
  - **Tests** (`u3b_a_slider_s_margin_takes_a_press`, `u3b_a_margin_takes_no_press_from_another_widget`,
    `u3b_a_scroll_bar_s_margin_takes_a_press`): without the flag (and with the byte alone) the margin's cells are
    the window's; with it, across and along, the ends' values, a drag begun there, past the margin nothing,
    hidden nothing; a window in front, a modal window, and a neighbour slider in front with its own margin each
    keep their cells; a scroll bar's page from `UW_B`'s low byte, its along bits ignored, its thumb dragged from
    beside it. No golden frame changed.

**Tests:** `test_oodos_ui` gains six `u3b_*` cases (and the review's rounds, below, more) (a scene of UITBAR's, blocks 6 and 7, and the picker's block F):
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

**The code review, round 2 (2026-11-12):** the owner had H1-H6 fixed as recommended. Each has a case in
`test_oodos_ui` (`u3b_r2_*`) that fails without its fix; H5's cases guard `ui_open`'s existing check, and fail when
`SPRCHK`'s pointer and size tests are taken out.
- **H1.** `ui_flags` on a slider or a scroll bar checks the new flags with `SPRCHK`, as `ui_open` and `ui_settext`
  do: `UF_SPRITE` set onto the pointer's sprite, or `UF_OWNTHUMB` with a size byte of 0, is refused with
  `UX_SPRITE`, carry set, the flags (and the rest of the record) as they were, nothing marked dirty. When
  `UF_SPRITE` goes off, the thumb's sprite goes out of sight (depth 0); set again, the next paint places it again,
  because `ui_flags` damages the record's area, which covers the thumb's last cells (POST's filter places a thumb
  whose cells the paint touched; round 3, J4, the wording). `u3b_r2_ui_flags_on_a_thumb_is_checked`.
- **H2.** A `UF_ROUTINE` routine of 0 calls nothing, and isn't refused: a canvas's rows are transparent; a list's
  rows are empty, in `TC_LIST` with no style (the selection's row still in its colour). For the list, empty rows
  were chosen over the array path (`UW_TEXT` 0 read as an array at `$0000`, which would draw whatever the zero page
  holds); no golden frame has such a list. `u3b_r2_a_routine_of_0_draws_nothing`.
- **H3.** The canvas's routine rules said beside the list's (above): no BDOS or FARCALL for the canvas's; called
  for each row drawn, maybe more than once a paint, its answer not to depend on the count. And the rows outside
  the paint's clip (the damage box inside the canvas's parents) are skipped, with no call and no `GX_VMAP`.
  `u3b_r2_a_canvas_s_routine_is_called_only_for_the_rows_redrawn`: a small window shown and hidden over two of the
  canvas's rows calls the routine for those two alone. **Cost:** the canvas's drawing in that strip's paint, 15,731
  cycles before (all four rows called, two of them drawn clipped away), 8,720 after. The whole canvas made dirty
  (`u3b_a_canvas_s_cells_from_a_routine`): its cells 21,907 before, 21,990 after (the row's clip test, and the test
  routine's call count, which this round added); its `ui_poll` 73,313 before, 74,039 after, against 66,122 and
  66,328 for a plain canvas, which doesn't reach the changed code: the difference is where the interrupts fall.
- **H4.** OOUI clears the decimal flag after each of the program's three callbacks: the canvas's routine, the
  list's row routine and (round 3, J1) the picker's column routine.
  `u3b_r2_a_routine_returning_with_d_set`: test routines that return with D set; without the `CLD` the canvas's
  row addresses come out in decimal and the program stops answering, and the list's frame changes.
- **H5.** `u3b_r2_ui_open_checks_a_scroll_bar_s_and_an_own_thumb_s_sprite`: `ui_open` refuses (`UX_SPRITE`) a
  scroll bar with `UF_SPRITE` on the pointer's sprite (with the program's thumb and with OOUI's), and a
  `UF_OWNTHUMB` thumb with a size byte of 0 (a slider's and a scroll bar's).
- **H6.** The cells' and the rows' pointer rule reads "in the program's memory, below `$4000` or in `$8000-$BFFF`"
  (the canvas, the list, their sources' headers).

**Sizes after round 2** (mkbank.py; a block's code window is 12,288 bytes): `UICOM` 3,298; block A 11,710 (578
free; 685 after round 1, 817 before U3b), B 10,537 (1,751 free; 1,803 after round 1, 2,396 before U3b), C 7,222
(5,066 free, unchanged); `UISTATE` 1,463 of 1,536, unchanged (round 2 added no variable; the canvas's are block
B's own).

**The code review, round 3 (2026-11-12):** the owner had J1-J4 fixed as recommended. Each code change has a case
that fails without it.
- **J1.** OOUI clears the decimal flag after the picker's column routine (`ROWFN` in block C), as after the other
  two callbacks (H4). `u3b_r3_a_column_routine_returning_with_d_set`: the test's column routine returns with D
  set; without the `CLD` the picker's rows (the entries' addresses, added in decimal) come out wrong.
- **J2.** `ui_flags` refuses (`UX_SPRITE`, carry set, the flags as they were, nothing dirtied) a change to
  `UF_OWNTHUMB` on a slider or a scroll bar while `UF_SPRITE` is on both before and after the call: the sprite's
  image would change hands while it shows (OOUI's image over the program's sprite, or the program's thumb left
  with OOUI's image). Taken: both set together from off, both cleared together, `UF_SPRITE` turned on or off in
  the same call as the change, and a change while `UF_SPRITE` is off (no sprite shows); `SPRCHK` still checks what
  results. `ui_open`'s and `ui_settext`'s checks are unchanged. `u3b_r3_ui_flags_keeps_an_own_thumb_while_its_sprite_shows`:
  each direction refused (a slider's own thumb made OOUI's, a slider's OOUI thumb made the program's, a scroll
  bar's own made OOUI's), then each allowed path. Round 2's case that sets `UF_OWNTHUMB` on `Z_HSC` with a size
  byte of 0 is now refused by J2 first; the same refusal, unchanged expectation.
- **J3.** `u3b_r2_a_routine_of_0_draws_nothing` also checks the selection's row in a list with a routine of 0:
  `TC_LISTCUR` without the focus, `TC_LISTSEL` with it, its neighbours `TC_LIST`. (It fails if the selection's
  colour is taken away.)
- **J4.** H1's note above says why the thumb is placed again: `ui_flags` damages the record's area, which covers
  the thumb's last cells.

And the margin for sliders and scroll bars ([above](#u3b-the-gaps-oo-amps-port-found)), a new gap the owner
approved with round 3.

**Sizes after round 3** (mkbank.py): `UICOM` 3,298, unchanged; block A 11,935 (353 free: J2 20 bytes, the margin
205), B 10,548 (1,740 free: the margin's scroll-bar page 11), C 7,223 (5,065 free: J1's `CLD`); `UISTATE` 1,463 of
1,536, unchanged.

**The code review, round 4 (2026-11-12, of 48c0386):** clean; K1 and K2 fixed with the repaint's changes below.
- **K1.** `UI_VSCROLL` and `UI_HSCROLL` refuse (an `.assert` in `_UI_PAGE`) a margin given without `UF_MARGIN` in
  the flags, since without the flag OOUI reads `UW_B` whole as the page and the margin byte would make it a page of
  256 or more. `ui_flags`'s row (above) says that setting or clearing `UF_MARGIN` on a scroll bar changes what its
  `UW_B` means, so the program sets `UW_B` again first. `check_tools.py` assembles each: both macros refuse a
  margin without the flag, take it with the flag, and take a scroll bar with no margin (a page of 300); without
  the `.assert` the refused sources assemble.
- **K2.** `u3b_r4_a_margin_at_the_edges_under_a_hidden_parent_and_a_drop_down`: a slider with `$11` at its window's
  client area's top left (the margin's cells on the frame and the title take nothing, those inside do); on the
  desktop at the screen's top left, its window hidden (below it and after its end take the press; nothing comes
  round from the bottom row or the right column); its window hidden (neither the margin nor the track); and a
  drop-down down over the margin (the press is the menu's item, the slider's value unchanged). It guards what
  `MTEST` does already: without its client-area test the frame cells take the press; without its hidden test
  the hidden window's slider takes it (and `u3b_a_slider_s_margin_takes_a_press` fails too).

#### U3b: the repaint (B1-B4) ####

**Proposed, built 2026-11-12**, the owner's choice from the repaint trace (OO-Amp's screen, about 90 records): B1,
B2, B3 and B4's cheap parts. No call, record or event changes meaning; no golden frame changed.

- **B1, no paint when nothing changed.** `UISTATE` gains one byte, `uany` (the one the owner approved), whose
  `UQ_PAINT` bit says a record was dirtied or damage added since the last paint. `DIRTY` sets it (so every call
  that changes a widget, `ui_show`, `ui_flags`, the focus, `US_HOT` with `TF_HOT`, the widgets' keys, presses and
  drags, the menus, the picker's listing, key repeat, which comes as a key), as do `BOXADD` (hiding, `ui_flags`'
  old place, a menu put up), `BOXFULL` (`ui_open`, `ui_theme`, `ui_redraw`, `ui_resume`) and `DLGREC` (the dialogs'
  records). `PAINT` returns at once while it is clear, and clears it with the damage, after the passes and before
  `POST`, so what dirties or damages from there on is the next paint's. The thumbs' placing (`POST`, Z3) follows a
  paint that drew or had damage (Z2), as before. `BOXADD`'s and `DLGREC`'s bits are kept for safety: every caller
  of each also dirties a record today, so no case fails without them.
- **B2, one pass on one layer.** On one layer (`UO_OVER` `$FF`, as OO-Amp's look A is), `PAINT`'s second pass is
  for records on the overlay; it is skipped when `OVERS` says none can be there.
- **B3, the modal window and the overlay kept.** `FINDMODAL`'s `umodal` and `OVERS`'s answer (`uany`'s `UQ_OVERS`)
  are found again only when `uany`'s `UQ_MSTALE` and `UQ_OSTALE` say so. `REFOCUS` sets both, and it follows every
  `ui_show`, `ui_hide`, `ui_flags` and dialog shown or hidden (`ui_msgbox`, `ui_pick`, their closing); so do
  `DLGREC`, `ui_open` (after `ui_close`), `ui_resume`, `ui_redraw`, and `ui_dirty` on a window: a program may
  write a record's fields itself and call `ui_dirty` (above), and a window's are what the two answers read. While
  suspended nothing reads them. A program that writes `UF_MODAL`, `UF_OVERLAY` or `UF_HIDDEN` itself on a record
  other than a window, or a record's type or parent, calls `ui_redraw` after (no such program is known).
- **B4's cheap parts.** The walks that look at every record in order (`PAINT`'s clearing of the dirty bits,
  `PASS`, `OVERS`) step from one record to the next (`RNEXT`, in `UICOM`: 16 bytes on, the dialogs' records after
  the table's) instead of finding each with `REC`, where nothing between moves `UIR`. And while the left button
  holds a slider or a scroll bar, a pointer move doesn't look for the widget under it (`DRAGGED`): the drag
  follows the pointer as before, and `US_HOT` stays where the press found it till the pointer moves after the
  release (a `TF_HOT` button isn't lit while a drag passes over it).

**What a poll costs among 90 records** (`u3b_r5_a_poll_s_cost_among_ninety_records`: block 8 of UITBAR, block 5's
table with 31 labels more, on one layer as OO-Amp's; the cycles of every `ui_poll` until `UE_NONE`; a frame is
119,318):

| What | Before (48c0386) | After | The limit the case holds |
| --- | --- | --- | --- |
| An idle poll | 56,679 | 878 | 5,000, no `PASS`, under 5 `REC`s |
| The first label dirty (the records after it placed) | 453,345 | 242,153 | 350,000, one `PASS` |
| The last label dirty | | 38,423 | under 150 `REC`s; `REC`s and steps under 330 |
| The last label shown again | | 45,572 | under 150 `REC`s |
| A drag's step (a band held, moved a row) | 628,883 | 324,284 | 450,000, no `FRONT` |
| A button pressed | 1,026,758 | 747,598 | 900,000 |
| ... and released (its click) | 455,661 | 222,613 | 350,000 |
| 30 widgets changed at once (14 sliders' values, 15 labels: a track's start) | 2,273,689 | 1,921,661 | 2,150,000 |

UITEST's idle poll (`what_a_call_and_a_crossing_cost`, 31 records) went from 20,728 to 878; its limit, 30,000, was
unchanged then (5,000 since round 5, below). What is left in the polls that draw is mostly `RECT`: once a dirty widget has been drawn, each record
after it is placed (its parents walked) to see whether it overlaps the damage, a few thousand cycles each, and the
sliders' thumbs placed after. Each change's must-fail, by hand on 2026-11-12: B1's skip taken out, the idle poll
18,303 cycles and a `PASS`; `DIRTY`'s bit, 32 cases fail; `BOXFULL`'s, 7; B2's skip, two `PASS`es; `FINDMODAL`'s
cache, 90 `REC`s idle and the modal case; `OVERS`'s, the steps' limit; `REFOCUS`'s, `ui_dirty`'s, `ui_resume`'s
and `ui_open`'s invalidations, `u3b_r5_the_modal_window_and_the_overlay_kept_are_found_again` (which sees a modal
window come and go after each, and the overlay's window's button take a click only while it shows), and more
cases for `REFOCUS`'s and `ui_open`'s; each step taken out, the `REC` limits; `DRAGGED`, the drag's `FRONT`.

**Sizes** (mkbank.py): `UICOM` 3,351 (53 more: `RNEXT`, `DIRTY`'s bit, `OVERS`'s cache, in every block); block A
12,117 (171 free, 182 used: `PAINT`, `PASS`, `FINDMODAL`, `REFOCUS`, the calls' invalidations, `DRAGGED`), B 10,601
(1,687 free), C 7,281 (5,007 free); `UISTATE` 1,464 of 1,536 (`uany`, the one byte approved).

#### U3b: round 5 ####

**Proposed, built 2026-11-12**, the owner's choices after the code review's round 5 (of 184bae4) and OO-Amp's test
pass. No U1, U2 or U3a call, record or event changes meaning; without the new flags nothing changes; no golden frame
changed.

- **`PASS` uses `RECT`'s sibling cache.** `FRONT` and `SHOWS` already turned it on (Z3); `PASS` now does too, for
  its walk, and turns it off at its end: a record whose parent is the last placed record's parent is placed from
  that parent's place, kept, instead of walking its parents again. 11 bytes of block A. In
  `u3b_r5_a_poll_s_cost_among_ninety_records`:

  | What | B1-B4 (184bae4) | Round 5 | The limit now |
  | --- | --- | --- | --- |
  | The first label dirty | 242,153 | 143,232 | 200,000 |
  | A drag's step | 324,284 | 241,404 | 290,000 |
  | A button pressed | 747,598 | 664,636 | 720,000 |
  | ... and released | 222,613 | 160,412 | 200,000 |
  | 30 widgets changed at once | 1,921,661 | 1,828,366 | 1,880,000 |

  Each limit fails without the cache (all five did, by hand, 2026-11-12); left on after `PASS` (its `stz rcv`
  taken out), three cases of menus and the pointer fail, a later `RECT` placing a record from a stale parent.
- **The idle poll's limit.** `what_a_call_and_a_crossing_cost` holds UITEST's idle `ui_poll` under 5,000 cycles
  (30,000 before; the owner accepted the change). It measures 877; without B1's skip it is 18,303.
- **L1 (the review's round 5): the `ui_dirty` row** ([the calls](#the-calls)) says that a program that writes a
  window's `UF_MODAL`, `UF_OVERLAY` or `UF_HIDDEN` itself calls `ui_dirty` on that window (or uses `ui_flags`,
  `ui_show`, `ui_hide`), as B3 already said. A documentation change only.
- **L2 (the review's round 5): accepted as documented.** `US_HOT` stays on a slider or a scroll bar after a drag
  until the pointer moves (B4's `DRAGGED`); no change.
- **A slider that refuses the wheel** (`UF_NOWHEEL`, `$80`, a slider's; OO-Amp's test pass, the owner 2026-11-12).
  OO-Amp's wheel moves its volume wherever the pointer is, except over the playlist, which scrolls. Without the
  flag, the wheel over a slider steps it, as before. With it, the slider's wheel handler (block B's `WHEEL`)
  takes nothing, and `ui_poll` returns what it returns for a wheel over a widget that takes none: `UE_WHEEL`, X the
  slider (the widget under the pointer), `UV_VALUE` the steps, signed. Its keys, its presses and drags, and its
  margin are unchanged. **Choice:** X is the slider, not the record that would be under the pointer without it;
  finding that one needs another walk in `FRONT`'s order (`UICOM`, every block), and OO-Amp acts on `UE_WHEEL`
  whatever X is. A list, a scroll bar or a canvas under a `UF_NOWHEEL` slider doesn't get the wheel (none in
  OO-Amp's layout).
- **A scroll bar without arrows** (`UF_NOARROWS`, `$80`, a scroll bar's). Without the flag, as before. With it:
  - the track is the whole bar (its length, not its length less 2), drawn without arrows, and the thumb's
    travel is the whole length: in cells, `(length - thumb) x value / last`; with a sprite thumb, units from the
    bar's first cell, room = length x 8 (4) less the thumb;
  - a press on an end cell is the track's, as anywhere on it: a page towards that end if the thumb isn't there,
    the thumb held if it is. There are no arrows, so no single steps from the pointer; the accelerator table's
    `UA_UP` and `UA_DOWN` and the wheel still step it;
  - the margin (`UF_MARGIN`) is as before, across only.
  The flag can be set or cleared with `ui_flags` (the record is damaged, so the bar is drawn again and its sprite
  thumb placed again).
- **What OO-Amp sets:** `UF_NOWHEEL` in the flags of every slider whose wheel should be the volume (the EQ's
  bands, `vol`, `bal`, `seek`: OO-Amp's own `UE_WHEEL` handling then moves the volume by its own step, also over
  `vol`), and `UF_NOARROWS` in `plscroll`'s, its length the whole 11 rows of its 88 pixels. No other byte: both
  flags are bit 7 of `UW_FLAGS`, free on sliders and scroll bars (bits 4-6 are `UF_SPRITE`, `UF_OWNTHUMB`,
  `UF_MARGIN`); `UI_*SLIDER` and `UI_*SCROLL` take them in their flags.
- **The cost:** no `UISTATE` byte, nothing in block A. Block B: the wheel's flag test 6 bytes; the bar 63 bytes
  and two bytes of block B's own memory (`BBSS`: the cells before the track, 1 or 0, and their units), read by
  `MEASURE`, `DRAW`, `ALONG`, `DOWN` and the sprite thumb's placing. A few cycles a call.
- **Tests:** `u3b_r5_a_slider_that_refuses_the_wheel` (both sliders of UITBAR's U2 block: the wheel up and down is
  `UE_WHEEL` with the slider and the steps, the value kept; a key and a press still move it; the flag cleared, the
  wheel steps it); `u3b_r5_a_scroll_bar_without_arrows` (UITWID's cell bar: the arrows' cells drawn as the thumb
  and the track, nothing past the end, the end rows page, the thumb dragged from the first row to the last gives
  the last value; UITBAR's block 6: the sprite thumbs at the bar's first cell and at the end of the whole length,
  the ends page, a drag, the horizontal thumb held on the bar's first cell; the flag cleared, the arrows back). By
  hand, 2026-11-12: each of eleven pieces of the code taken out or put back as it was (the wheel's test, the flag
  read, the length, `ALONG`'s vertical and horizontal offset, the thumb's cells, its row and column, `DRAW`'s cell
  and its end, `DOWN`'s) fails one of them.

**Sizes after round 5** (mkbank.py): `UICOM` 3,351, unchanged; block A 12,128 (160 free: the cache 11), B 10,670
(1,618 free: 69), C 7,281 (5,007 free, unchanged); `UISTATE` 1,464 of 1,536, unchanged.

#### U3b: speed step S1 ####

**Proposed, built 2026-11-12**, the owner's choice from the speed design (at cb1f81f): three of its changes, (i),
(v) and (viii), and one rule. No call, record or event changes meaning but for the rule; no golden frame changed;
`UISTATE` unchanged (1,464 of 1,536): the new state is three free bits of `uany` and one of each thumb's
`UW_STATE`.

- **(i) A thumb placed only when it can have moved.** `POST` placed every sprite thumb whose cells the paint's
  damage touched, and a paint's damage is the box round everything it drew: on OO-Amp's screen a track's start
  (some 27 labels) placed all 15 thumbs, each a walk of the records (`SHOWS`). Now `POST` places a thumb if
  - its record was drawn dirty, or its value changed, since the last `POST`: the record's `UW_STATE` bit 7
    (`US_THUMB`, `ui.inc`'s; free on every type, `US_*` being bits 0-5), set by the widget's drawing when the
    record is dirty (`SPRDRAWN`, block B) and by a value's change (`VDIRTY`, below), cleared by `POST`; or
  - it has no last place (just opened, or a new sprite: as before); or
  - a geometry event has come since the last `POST` (`uany`'s `UQ_GPOST`) and the thumb's cells are in the
    damage (U3a's Z3 filter, as before).
  Otherwise it is left as it was: drawing a record never changes what is in front of a thumb or where it is.
  **The geometry events** (`UQ_GEOM`: `UQ_GPOST` and `UQ_GHIT`, below, set together), every path that can change
  what covers what, or where a record is:

  | Path | Where it is set |
  | --- | --- |
  | `ui_show`, `ui_hide`, `ui_flags`, a dialog's widget shown or hidden, the box's and the picker's coming and going | `REFOCUS` (each ends there) |
  | the dialogs' records made (`ui_msgbox`, `ui_pick`) | `DLGREC` (with `REFOCUS` after: kept for safety) |
  | a menu down (by the keys or the mouse; its drop-down's size from its items' texts, so `ui_settext` on one too) | `MPASS`, at each paint while a menu is down |
  | a menu up | `MCLOSE` |
  | `ui_open`, `ui_redraw`, `ui_resume`, `ui_theme` (the thumb image's colours are the theme's) | `BOXFULL` (and `ui_open`'s own setting of `uany`, `STALE`: kept for safety) |
  | `ui_dirty` on a window (its flags the program's own writing), `ui_redraw`, `ui_resume` | `STALE` |

  Not geometry: a record drawn (`DIRTY`, the damage it adds), the focus, `US_HOT`, a press or a drag, a list's
  rows, a value. `POST` clears `UQ_GPOST` (and `UQ_THUMB`) when it has looked at every thumb.
- **(v) The hit test kept while nothing moved.** A press, a release and the wheel each walked the records
  (`HIT`, `FRONT`) for the widget under the pointer, which `POINTED` had already found (`uhot`) if the pointer
  hadn't moved since. `HITQ` (block A) answers `uhot` while the pointer is where `POINTED` last saw it (`lmx`,
  `lmy` against the event area's `EV_MX`, `EV_MY`) and `uany`'s `UQ_GHIT` is clear; else `HIT`. `UQ_GHIT` is set
  by every geometry event above, and by a pointer move (`POINTED`) until its `HIT` has run, so a drag's moves,
  which look for nothing (B4's `141,465, nothing drawnGED`), leave it set; `POINTED` now finds the modal window again
  (`FINDMODAL`, which costs nothing while it is kept) before its `HIT`, as the press's did. **Choice:** the
  release (`MUP`) uses `HITQ` too, not only the press and the wheel.
- **(viii) A sprite thumb's value draws no cells.** A slider's or a scroll bar's cells with `UF_SPRITE` don't show
  its value (the thumb is the sprite), so a value's change there (the widget's keys, press, drag and wheel, and
  `ui_setvalue`) marks the thumb (`US_THUMB`) and a paint to do (`UQ_PAINT`, and `UQ_THUMB` so that `POST` runs
  though nothing was drawn) instead of dirtying the record: no cell drawn, no damage, so nothing round it drawn
  either. `VDIRTY` (block B) does it; `ui_setvalue` reaches it by a crossing (`XB_VDIRTY`) only for a record with
  bit 4 of its flags set (`UF_SPRITE` on these types; the other types' own bit 4 costs them the crossing, and
  `VDIRTY` dirties them as before). A cell thumb's slider or scroll bar, and `ui_setrange`, `ui_settext`,
  `ui_flags` and the focus on a sprite one, still dirty the record as before.
- **The rule (owner-approved):** a program that moves or resizes a record by writing its fields (or changes its
  type or parent) calls `ui_redraw` after; `ui_dirty` is not enough, since it says nothing about what covers what
  ([widget records](#widget-records), [the calls](#the-calls)'s `ui_dirty`).
- **Accepted** (the owner, with S1): round 5's choice that a `UF_NO138,121` slider's `UE_138,121` has X the slider.

**What it costs** (`s1_a_poll_s_cost_among_ninety_records`, block 8 of UITBAR, 90 records on one layer as
OO-Amp's; every `ui_poll` until `UE_NONE`; a frame is 119,318 cycles):

| What | Before (cb1f81f) | S1 | The limit the case holds |
| --- | --- | --- | --- |
| A track's start (the seek bar's and the playlist slider's values, the title, 26 track labels) | 1,679,598, 15 thumbs placed | 378,777, two placed | 450,000, two thumbs placed |
| A drag's step (a band held, moved a row) | 241,390, 74 records drawn | 141,465, nothing drawn | 170,000, nothing drawn, one thumb placed, no `FRONT` |
| The wheel over the band, the pointer still | 311,900 | 138,121 | 170,000, no `FRONT`, nothing drawn |
| A button pressed, the pointer still | 608,801, 4 thumbs placed | 261,697, one thumb placed | 330,000, no `FRONT`, at most one thumb (the band's, which the focus left) |
| ... and released (its click) | 161,069 | 104,694 | 130,000, no `FRONT` |
| 30 widgets at once (15 sliders' values, 15 labels) | 1,829,831 | 1,554,535 | 1,650,000, the 15 thumbs placed |

What is left is mostly `SHOWS`, a walk of the records in `FRONT`'s order for each thumb placed (the drag's step,
the 30 widgets' 15 thumbs), and `RECT` for each record after a dirty one in `PASS`: S2's and S3's. In
`u3b_r5_a_poll_s_cost_among_ninety_records` (its limits unchanged) the press with the pointer moved there first
falls from 664,636 to 317,902.

**Tests:** `s1_a_poll_s_cost_among_ninety_records` (above); `s1_a_thumb_follows_every_geometry_event` (UITBAR's
U3b scene: the 8 x 32 thumb covered and uncovered by a window shown and hidden, a drop-down by the keys and by the
mouse, a window the program un-hid itself with `ui_dirty`, a window the program moved over it and back with
`ui_redraw`, the message box; kept hidden meanwhile while a label and its window are drawn and its own value
changes; `ui_flags` taking the window's frame away moves every thumb a cell though no thumb's record is dirty;
`ui_suspend` and `ui_resume`; `ui_theme` rewrites OOUI's own thumb image in the new colours);
`s1_a_thumb_s_own_changes_place_it` (`ui_setrange` moves a thumb; a value's change writes no cell of the track;
`ui_dirty` draws it again); `s1_a_hit_is_found_again_after_each_geometry_event` (a window shown under the still
pointer takes the press and the release, and the wheel; one the program un-hid with `ui_dirty` too; a drag let go
over a button, then a press there without a move, is the button's; a modal window the program made itself keeps a
press, the pointer moved onto a button outside it first). `polls_cost` also counts `ONE` (a record drawn) and
`PLACE1` (a thumb placed). By hand, 2026-11-12, each piece taken out: (i)'s skip of a thumb without a geometry event, the track's start's thumbs and cycles and the press's thumbs;
`POST`'s clearing of `UQ_GPOST`, the same; `SPRDRAWN`'s mark, `ui_settext`'s new size byte
(`u3b_ui_settext_on_a_thumb_is_checked`) and `ui_setrange`'s move; `PAINT`'s `UQ_THUMB`, seven cases (no value's
change placed); `MPASS`'s and `MCLOSE`'s bits, the drop-downs (and `u3b_a_tall_thumb_covered_at_one_end_is_hidden`);
`REFOCUS`'s, seven cases; `STALE`'s, `ui_dirty` on the window; `BOXFULL`'s, `ui_theme`'s image; (viii)'s `VDIRTY`
dirtying instead, the drag's and the wheel's drawing and cycles, the 30 widgets' cycles and the track's cell;
`ui_setvalue`'s crossing skipped, the track's start's case and the cell; (v)'s `HITQ` made `HIT`, the `FRONT`
counts and cycles; the drag's `UQ_GHIT`, the press after a drag; `POINTED`'s `FINDMODAL`, the program's own modal window; the
pointer's comparison, the press where the pointer moved after `POINTED` looked. `DLGREC`'s and `ui_open`'s bits fail
nothing taken out alone (`REFOCUS` and `BOXFULL` follow each): kept for safety, as B1's were.

**Sizes** (mkbank.py): `UICOM` 3,351, unchanged; block A 12,233 (55 free; 105 bytes: `HITQ` 36, `ui_setvalue`'s
test and crossing 33, `POINTED`'s bits and `FINDMODAL` 15, `MPASS` 8, `MCLOSE` 8, `PAINT`'s `UQ_THUMB` 5), B
10,740 (1,548 free; 70: `SPRDRAWN`, `VDIRTY`, `POST`'s tests, `BTAB`'s entry), C 7,281 (5,007 free, unchanged);
`UISTATE` 1,464 of 1,536, unchanged.

#### U3b: an own thumb's length ####

**Proposed, built 2026-11-12** (the owner, after OO-Amp's port at cc500d2). A scroll bar's sprite thumb was as long
as its sprite along the track; OO-Amp's playlist bar (`plscroll`: 11 rows, 88 pixels, `UF_SPRITE`,
`UF_OWNTHUMB`, `UF_NOARROWS`) draws a 20-pixel thumb in the top of an 8 x 32 sprite, so it travelled 88 - 32 = 56
pixels, not 68. Opt-in and additive:

- **The byte.** A scroll bar has no free byte, and no free flag (bits 4-7 are `UF_SPRITE`, `UF_OWNTHUMB`,
  `UF_MARGIN`, `UF_NOARROWS`). With `UF_MARGIN` its margin byte is `UW_B`'s high byte, whose high four bits (the
  cells beyond each end along the track) do nothing on a scroll bar ([the margin](#u3b-the-gaps-oo-amps-port-found)).
  On a bar with `UF_OWNTHUMB`, `UF_NOARROWS` and `UF_MARGIN` together, those four bits, not 0, are the thumb's
  length in 4 units (a unit is a pixel: 4 to 60), used instead of the sprite's for its room (the track's units
  less the thumb's), its place, the cells `SHOWS` and `POST` look at, and a press on it (held inside its length,
  a page past it). 0 is the sprite's length, as before; the sprite is placed with its start at the thumb's
  start, so at the last value its last 12 pixels lie past the bar's end, where OO-Amp's art has room.
- **Choice:** only with all three flags. On a bar with arrows the bits keep doing nothing, as
  `u3b_a_scroll_bar_s_margin_takes_a_press` holds them (its `$11`); OOUI's own 8 x 8 thumb is its whole sprite.
  The alternative, any scroll bar with `UF_MARGIN`, would change that case's input. A slider needs nothing: its
  thumb is centred on its value whatever its size.
- **What OO-Amp sets:** `plscroll`'s `UW_B` = `$51` << 8 \| the page (high four bits 5: 20 pixels; low four 1:
  the margin's column each side, as before), its flags `UF_SPRITE | UF_OWNTHUMB | UF_MARGIN | UF_NOARROWS`
  (`UI_VSCROLL`'s margin argument `$51`).
- **The cost:** 24 bytes of block B (`MEASURE`), nothing in A, C or `UISTATE`; a few cycles a measure.
- **Test:** `u3b_an_own_thumb_s_length_apart_from_its_sprite` (UITBAR's `Z_VSC` made `plscroll`'s shape: 11 rows,
  `$51`): the thumb at 0, 34 and 68 pixels for 0, 50 and the last value, shown at the end; held 5 units in and
  moved 34, the value 50; a press 25 units in, past the 20, a page; the bits 0, the travel 56 again. Fails five
  ways with the length not taken (by hand, 2026-11-12). No golden frame changed.
- **Sizes after it** (mkbank.py): `UICOM` 3,351; block A 12,233 (55 free); B 10,764 (1,524 free); C 7,281 (5,007
  free); `UISTATE` 1,464 of 1,536.

#### U3b: speed step S2, the position cache ####

**Proposed, built 2026-11-12**, the owner's choice after the profile of OO-Amp's slowest response (its EQ ON click,
2.78 frames at cc500d2: of the paint's 180,189 cycles in `PASS`, 83,730 were 69 records placed by `RECT` and not
drawn, about 1,210 each). No call, record or event changes meaning; no golden frame changed; `UISTATE` 1,462 of
1,536 (2 bytes fewer: `FRONT`'s two left it).

- **The cache.** Block C's own memory (`CBSS`, uicache.s) holds an entry for each record, the table's and the
  dialogs' (254 at most, 5 bytes each, 1,270 bytes): where it shows, its cells inside its parents' client areas
  (`c0`, `r0`, `c1`, `r1` from `RECT`'s place and `rclip`), and three flags: hidden (or a record no walk looks
  at, unused or a menu's), on the overlay (`rover`), casting a shadow (a framed window, the theme's `TF_SHADOW`).
- **The walks read it, in block C.** `PASS`'s walk, `FRONT` (from `UICOM`), `HIT`, `MARGIN` (block A's) and
  `SHOWS` (block B's) are uicache.s's. `PASS` places a clean record (`RECT`) only if its entry meets the box: `CMEETS`
  tests the cached cells, clipped to the parents' client areas, with the shadow added, which is narrower than
  `ONE`'s `OVERLAP` (the record's whole place, `EXTRECT`'s `er`, unclipped): a record it passes over meets the
  box only where its parents clip it, where `ONE` would draw nothing; `ONE` still decides for the records it
  passes; `FRONT` and `SHOWS`
  read the entries only; `MARGIN` places each flagged slider or scroll bar by `RECT`, which refuses a hidden one. A, the core,
  keeps `PASS`'s clearing of the box (`PASSA`) and crosses to C for the walk (`XC_PASS`, once a layer), C to A's
  `ONE` for each record it draws (`XA_ONE`); a press or a pointer's move crosses for `HIT` (`XC_HIT`), a press for
  `MARGIN` (`XC_MARGIN`), and each sprite thumb placed for `SHOWS` (`XC_SHOWS`: its last cell in A and X, its
  record in Y). `POST`'s own walk stays in B (a `REC` and a type a record). **Choice:** `SHOWS` alone moved, not
  `POST` with it: the same one crossing a placed thumb, less code moved.
- **Stale and found again.** `uany`'s free bit, `UQ_CSTALE` ($20), says the cache is stale; it is set with the
  geometry events (S1's table) but a menu's (`MPASS`, `MCLOSE`: the menus aren't in it): `BOXFULL` (`ui_open`,
  `ui_redraw`, `ui_resume`, `ui_theme`), `ui_open`'s own setting of `uany`, `DLGREC`, and `STALE` (`ui_dirty` on a
  window). The next walk finds it again whole: `PASS` as it places every record (its first, "filling", mode)
  and writes every unused record's and menu's entry hidden, or `FRONT`, `MARGIN` and `SHOWS` by `CFILL` (every
  record placed, about 880 cycles each; the unused and the menus' hidden); either writes every entry the walks
  read (the table's and the dialogs' records, `utot`). `ui_open` first makes every entry hidden (`CCLR`, all
  254, through `XC_CCLR`), so none is the last table's or the bank's memory as `ui_load` left it (the review's
  round 8, P1: `PASS`'s filling had left the unused records' and the menus' entries as they were). `ui_show`, `ui_hide` and
  `ui_flags` (the dialogs' too, through `HIDEH`) call `CSET` (`XC_CSET`) instead: a record no record is the
  child of is placed alone and its entry written; one with children makes the cache stale.
- **A dirty record compared.** `PASS` places every dirty record, as before, and compares it with its entry: a
  difference (a record the program moved by its fields and only dirtied, against the S1 rule) makes the cache
  stale and is a geometry event (`UQ_GEOM`), the rest of that walk placing every record as before S2.

**What it costs** (`s2_a_poll_s_cost_with_the_position_cache`, block 8 of UITBAR, 90 records on one layer as
OO-Amp's; every `ui_poll` until `UE_NONE`; a frame is 119,318 cycles; before, the same case with 20caf52's
OOUI.BNK):

| What | Before (20caf52) | S2 | The limit the case holds |
| --- | --- | --- | --- |
| One label dirty, the table's first | 142,225, 88 records looked at by `ONE` | 52,558, one | 80,000, one `ONE` |
| A track's start (2 values, 27 labels) | 378,239 | 191,026 | 260,000, two thumbs placed |
| A button pressed, the pointer moved there (a `FRONT`) | 387,426 | 201,563 | 260,000 |
| ... and released (its click) | 104,317 | 50,008 | 80,000 |
| A drag's step (a band held, moved a row) | 141,830 | 69,882 | 100,000, one thumb placed |
| 30 widgets at once (15 thumbs placed) | 1,555,648 | 399,574 | 600,000, the 15 placed |
| `ui_redraw` (every record drawn, the cache found again) | 2,558,586 | 1,494,106 | measured |
| The last label shown again (`CSET`, not the whole cache) | 45,655 | 48,295 | 60,000 (158,612 with the cache found again whole) |

A crossing costs 322 cycles (`what_a_call_and_a_crossing_cost`). In the other cost cases (their limits unchanged)
B1-B4's `u3b_r5_a_poll_s_cost_among_ninety_records` and S1's `s1_a_poll_s_cost_among_ninety_records` all fall:
the 30 widgets 1,554,976 to 399,314, a press 318,259 to 125,764, a track's start 378,976 to 190,921.

**Tests.** `s2_the_position_cache_holds_every_record_s_place` (UITBAR's U3b scene): after each step of a
session, the pointer moved and a poll (a `HIT`, so a stale cache is found again: it must not be stale after), every
table record's entry is checked against `ui_rect` (its place, `rclip` and `rover` after it): a window shown and
hidden, a label hidden and shown (`CSET`), a window's frame gone and back, a label disabled, the menu bar and a
drop-down, the message box and its going, a hidden window and a label moved by their fields with `ui_redraw`,
`ui_suspend` and `ui_resume`, `ui_theme` 1 and 0, and a label and a window moved by their fields with only
`ui_dirty`. `s2_a_poll_s_cost_with_the_position_cache` (above). `polls_cost` counts `PASS` in block C. **Changed
expectation** (the owner's S2 design, its crossings): `the_picker_s_crossings_a_listing_poll_and_a_selection_move`,
a listing poll 3 crossings to 6 and a selection move 6 to 11 (each paint's two walks, each record drawn; 10 with
B5, below): **accepted** by the owner (2026-11-13). `s2_a_new_table_s_menus_and_unused_records_take_no_hit`
(round 8, P1): a click where the last table's radio button was, after `ui_close` and `ui_open` of a table whose
record there is a menu's item, is nothing; and the second window, over the Cancel button at its first column,
made unused by the program (its type 0) with `ui_redraw`, the pointer still: the paint fills the cache before the
press's hit test, and the press is Cancel's. It fails at 3cb4a1c (the second part: the window's stale entry takes
the press) and with `PASS`'s hidden entries taken out. `ui_open`'s `CCLR` can't be made to fail it alone: the first
`ui_poll` after `ui_open` hit-tests (`POLLINIT` sets the pointer's last place to $FFFF, so it has moved), and
that `CFILL` writes every entry before any is read, so the first part passes at 3cb4a1c too. The exception, and
`CCLR`'s reason: a program that calls `ui_suspend` right after `ui_open`, before any poll. `POINTED` is skipped
while suspended, and `ui_resume` marks the cache stale without resetting the pointer's last place, so with the
pointer still the next poll paints, and `PASS` refills, before any hit test; at 3cb4a1c that read the last
table's entries for unused and menu records. `CCLR` means no entry ever holds another table's place.
By hand, 2026-11-12, each piece taken out: the dirty record's comparison, the guard (the label moved and only
dirtied); `CSET` made nothing, the guard (the frame gone under a theme without shadows: the window's own entry is
the same, its children's places aren't) and the cost case's label shown again (158,612: the whole cache found
again); `CMEETS`'s shadow, the guard (a label under a window's shadow dirtied: the frame changed); the entry's
clipping to `rclip`, `x4_a_thumb_under_a_dialog_or_clipped_is_out_of_sight` and
`x5_a_clipped_away_part_takes_no_pointer`; `PASS` never reading the cache, and `CFILL` finding it again at every
walk, the cost case (3 and 4 limits). Kept for safety, failing nothing taken out alone (the comparison at the
paint catches each, the records they change being dirty): `UQ_CSTALE` in `BOXFULL`, `ui_open`, `DLGREC` and
`STALE`; and `FRONT`'s test of the hidden flag (a hidden entry has no cells).

**OO-Amp** (9c64da3, which draws an event's patches again, 865a981; `test_accept`'s response case, 1 minute):
the worst response done, of the keys and clicks that read no disk, 3.5 frames before S2 (20caf52's OOUI.BNK: 12
over 2, the shuffle and repeat clicks 2.8-3.0, EQ panel 3.5) and 2.4 with it (G EQ 2.4, the EQ panel's second
click 2.0; every other 1.9 or less). What is left in G EQ's 2.38 frames (283,862 cycles): 68,844 waiting for the
blank, the key's poll 42,000, the paint 144,552 (80,082 of it drawing 10 records, 8 of them only because the one
box joins the two buttons dirtied, 66,8 and 22,15: B5's), `vis_frame` 19,402; by code, OOUI 174,814 cycles,
OO-Amp's own 90,567, OODOS's and the system's 18,481.

**Sizes** (mkbank.py): `UICOM` 3,218 (133 fewer: `FRONT`); block A 11,833 (455 free; 55 before), B 10,433 (1,855
free; 1,524), C 9,591 (2,697 free; 5,007, the cache 1,270 of it); `UISTATE` 1,462 of 1,536.

#### U3b: B5, the damage in boxes ####

**Proposed, built 2026-11-12**, the owner's choice after S2's profile of OO-Amp (its G EQ key and EQ ON click
dirty two buttons, at 66,8 and 22,15: the one damage box joined them, and 8 records inside it but meeting
neither were drawn again, some 76,000 cycles). No call, record or event changes meaning; no golden frame
changed.

- **The boxes.** Each layer's damage is up to `UI_BOXES` (4) boxes, not one: box *k* of layer *L* (0 the base,
  4 the overlay) at `udmg` + 8 × *k* + *L*, so box 0 is where the one box was. `BOXADD` joins a rectangle with
  every box it overlaps (and the union with any it then overlaps), else puts it in the layer's first empty box;
  with none empty it joins the layer's last. Boxes that only touch (a row each, one under another) stay apart.
  `BOXFULL` makes box 0 the screen and the others empty.
- **The paint.** `PASSA` clears each box; block C's walk draws a clean record if it meets any box (`CMEETS`
  looks at each box up to `udend`'s, the end of the layer's boxes this paint has used; box 0 empty means all
  are, as `BOXADD` fills the first empty one); `ONE` draws it once for each box it overlaps, clipped to that box
  (placed again by `RECT` before a second); a dirty record is drawn whole and its rectangle added, as before.
  The overlay's own layer joins each of the base's boxes, as it joined the one. `POST`'s `pdmg` keeps each
  layer's boxes' bounds, so its test (`TOUCHED`) looks at no less than before.
- **Choice:** 4 boxes a layer (the owner allowed 2 to 4); a fifth rectangle joins the last box.
- **`UISTATE`:** 1,492 of 1,536 (30 bytes more: `udmg` 24, `bxl` 1, `udend` 2, `ONE`'s `obx`, `obh` and `odn`
  3), 44 free.

**What it costs** (block 8 of UITBAR; S2 before, the same case with 36d447c's OOUI.BNK):

| What | S2 | B5 |
| --- | --- | --- |
| Two far-apart labels dirty (A_TIME, top left; A_ST3, record 47, bottom right) | 166,816, 33 records drawn | 61,729, two |
| One label dirty | 52,558 | 53,826 |
| A track's start (2 values, 27 labels) | 191,026 | 210,684 |
| A button pressed, the pointer moved there | 201,563 | 131,210 |
| A drag's step | 69,882 | 70,647 |
| 30 widgets at once | 399,574 | 411,709 |
| 26 track labels dirty (more rows than boxes) | 138,269 | 157,709 |

OO-Amp's `test_accept` response case passes with B5 (worst 1.8 frames at 9c64da3, 1.9 for the keys at 7665e91;
[the outcome](#u3b-the-outcome)). A paint that draws one record or a few costs a little more (each record's test looks at each box used; a
dirty record's `BOXADD` looks at every box); one whose dirty records are apart, much less. **Accepted** by the
owner (2026-11-13), as are boxes joined only when they overlap, not when they only touch.

**Tests.** `b5_two_far_apart_dirty_records_draw_nothing_between_them` (the two labels above: two `ONE`s, under
90,000 cycles, OOUI's cells then as `ui_redraw` draws them; a label and the window under it, joined; the 26 track
labels, more than 4 boxes). `b5_each_box_is_drawn_again_whole` (UITBAR's U3b scene: two far-apart records
hidden at once, the window under both drawn in both holes; two far-apart records dirtied with a window over the
second, the window drawn again in the second box; the cells as `ui_redraw`'s). The tests compare OOUI's base
layer's cells, not the frame (the frame has a sprite that moves), from column 13 (the test program's replies
are written to the console layer's first columns). **Changed expectation:**
`the_picker_s_crossings_a_listing_poll_and_a_selection_move`'s selection move, 11 crossings to 10 (one record
fewer drawn: B5's): **accepted** by the owner (2026-11-13).
By hand, 2026-11-12, each piece taken out: `BOXADD` joining every box (one box again), the two far-apart labels'
case (33 records drawn); `CMEETS` looking at the first box only, `udend` not raised, and `ONE` drawing for the
first box it overlaps only, `b5_each_box_is_drawn_again_whole`; `pdmg` the first box's only, four of the thumbs'
cases (`x4_a_menu_down_over_a_thumb_hides_it`, `c1_a_thumb_shows_again_when_what_covered_only_it_goes`,
`u3b_a_tall_thumb_covered_at_one_end_is_hidden`, `s1_a_thumb_follows_every_geometry_event`). Failing nothing taken
out alone: `PASSA` clearing a box past the first (no scene has a hole no record draws over; the overlay's layer is
clear whole once its last window goes) and the overlay's layer joining a base box past the first; kept, as the
one box's were.

**After the review's round 8** (P1: `CCLR`, `PASS`'s hidden entries; `ui_open`'s own clearing of the boxes and
`udend` moved into `CCLR`, `ALLDIRTY`'s `BOXFULL`s emptying every box; `MARGIN`'s cache test, which `RECT` makes
needless, and `CEMPTY` folded into `PASS`, so OOUI.BNK stays 40,960 bytes, the 20 blocks of 2 KB that
`u2_the_picker_on_the_card_s_drives` leaves it on its M:): `UICOM` 3,218; block A 12,159 (129 free), B 10,433
(1,855 free), C 9,607 (2,681 free); `UISTATE` 1,492 of 1,536.

**Sizes before round 8** (mkbank.py): `UICOM` 3,218; block A 12,160 (128 free: `BOXADD` 289 bytes more, `ONE`'s and `PAINT`'s
loops), B 10,433 (1,855 free), C 9,589 (2,699 free); `UISTATE` 1,492 of 1,536.

#### U3b: the outcome ####

**Decided (the owner, 2026-11-13; it replaces 2026-11-12's).** Size was the wrong gate: a framework's ease and
consistency is worth a little size. **The port lands.** U3's gate is now: OO-Amp runs on OOUI with every feature
working, no unapproved change in behaviour, start-up and dialogs within an agreed time. It is met once the
landing (OO-Amp's feat/ooui-u3b, this repository's feat/ooui-u3b-land) is approved. The agreed times are the
owner's, and *decided (2026-11-13, L1):* start-up, from the CCP taking OOAMP's CR to the finished screen, at most
1.5 s (the port measures 1.48 s against 6502 616c968, OO-Amp's own UI 0.97 s), and a dialog shown whole (H to Help
shown whole) at most 0.4 s (0.37 s; 0.20 s on its own UI). OO-Amp's Load Files' "Add all" gives at most 40 files
(its slot's room; the old dialog 48): accepted (L7).

The size figures stay, as information (OO-Amp's feat/ooui-u3b against its main):

| What | OO-Amp's own UI | On OOUI |
| --- | --- | --- |
| OO-Amp's own code and data | 30,871 bytes | 29,958 (3% less) |
| Its files on the card | 85,840 bytes | 141,564 (OOUI.BNK 40,320 of them) |
| Start-up to the main loop | 56.0 frames (0.93 s) | 90.0 (1.50 s) at 20caf52; 87.2 (1.45 s) at 616c968: `ui_load` 17.5 frames, the first paint 12.3 against 2.3 |
| Help opened | 10.7 frames | 21.1 (test_accept's response, an MP3 playing); shown whole 22 frames against 12 (616c968, idle) |

And OO-Amp still draws its own pixel chrome: OOUI took over its widgets, not its look. `ui_load` already reads
OOUI.BNK by `DM_LOAD` (2026-11-12's "revisit when OOUI loads by DMA" was wrong); why 40 KB takes 17.5 frames is
the next small phase's question.

- **The behaviour part 4 changed** (the owner's dispositions, 2026-11-13): the picker's U key (the next user)
  and its size column are restored (below); accepted: a click while CONFIRM QUIT's box is up does nothing (it was
  a no), and a click outside a modal window is OOUI's to drop.
- **The picker passes on the keys it doesn't use** *(U3b landing; a change to existing behaviour, accepted by the
  owner 2026-11-13, L6)*: a key that no widget, button letter, Escape, Tab or the accelerator table takes is the
  program's `UE_KEY` while the picker is up, X the focus (one of the picker's handles), as it is outside the
  picker; before, the picker's filter dropped it. A program that acts on every `UE_KEY` sees these too. OO-Amp's
  U is one. `test_oodos_ui`'s `u3b_the_picker_s_add_and_add_all_keep_it_up` checks it.
- **The layers at the first paint** *(U3b landing; a change to existing behaviour, accepted 2026-11-13, L5)*:
  `ui_open` showed OOUI's layers before anything was painted (OO-Amp's start: its chrome without words for a
  frame, then the console, then the screen). They now show at the end of the first paint
  ([Opening and closing](#opening-and-closing)); `u3b_the_layers_show_at_the_first_paint` checks it. It costs 12
  bytes of UICOM, so OOUI.BNK is 41,088 bytes. So that OOUI's size no longer depends on M:'s room, the
  tests load it as the real cards and OO-Amp's tests do *(the owner's decision, 2026-11-13)*: the rig writes
  OOUI.BNK on A:, user 0, before OODOS boots (oodos_rig.h's `before_boot`, test_oodos_ui's `put_bnk_a`), and the
  test programs load it by `ui_load`'s default. M:'s file counts are as they were; A:'s listings show OOUI.BNK
  (the expectations that list A: count it: the owner accepted that in kind).
- **The bitmap backend: not now.** A 4-bit cell is 32 times a text cell's writes, and the backend would take
  some 38 KB of card memory; the port's pixel needs were met by sprite thumbs, margins and a thumb's own length.
- **OOUI's open gaps, for later:** Enter on a focused `UT_CHOICE` changes its value; `ui_theme`, and hiding a
  full-screen overlay window, draw again the widgets it covers; the picker takes 12-14 frames to switch user (made
  again by the program, as OO-Amp's U does: L4). (The modal picker no longer swallows the keys it doesn't use: the
  landing passes them on, L6.) *(Proposed, found by UIDEMO, 2026-11-13:)* in the blue theme (`UI_TURBO`) a slider's
  text thumb and a list's scroll bar barely show against what is round them; and a click on a radio button or a
  list's row gives no `UE_FOCUS`, as a click on a check box or a button does. *(Proposed, the UIDEMO review's Y1:)*
  `ui_close` leaves the console's cooked key (KEY) strobed; a drain there would cover every program (UIDEMO drains
  it itself before `ui_close`).
- **An S3 target** *(the owner, 2026-11-13)*: one key's change on screen within a frame. OO-Amp's
  `a_key_s_state_changes_within_a_frame` measures 1.24 frames (its limit 1.1): OOUI's paint in that key's pass.
  It is a known failure until S3.
- **The response case, as evidence** (OO-Amp 7665e91, dialogs on OOUI and patches drawn; `test_accept`'s response
  case, 1 minute; the worst done of the keys and clicks that read no disk, 2.0 frames its limit, frames):

  | OOUI.BNK | Keys (7665e91) | Clicks (9c64da3) | The worst |
  | --- | --- | --- | --- |
  | 20caf52 (before S2) | G EQ 2.6, G 2.7, S 2.4 | EQ panel 3.5, shuffle 3.0, repeat 2.8 | 3.5 (12 over 2) |
  | S2 (36d447c) | G EQ 2.6, G 2.7 (the one box), S 1.6 | EQ panel 2.0, the rest 1.8 or less | 2.7 |
  | B5 (a332d19) | G EQ 1.9, Tab 1.9, G 1.7 | playlist 1.8, EQ ON 1.0 | 1.9 at 7665e91, 1.8 at 9c64da3: **passes** |

  At 7665e91 the case can't reach its clicks with any OOUI.BNK (20caf52's too): from Load Files on every action
  is "never" (99 frames), so they are 9c64da3's, which has the same clicks. *(U3b landing:)* the cause was OO-Amp's, not OOUI's: the
  jump, made after Settings in the dialogs' shared records, kept Settings' CANCEL as its window's child, so Esc
  pressed that and the jump stayed up; fixed in OO-Amp (9df4444, with a test that fails without it). What is left in B5's G EQ (1.86
  frames, 221,653 cycles): 68,952 waiting for the blank, OOUI 96,711 (the paint 57,624, two buttons drawn), OO-Amp's
  own 111,711, the system's 13,231; before S2 the paint alone was 202,000.

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

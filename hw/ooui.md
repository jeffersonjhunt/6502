# OOUI: the OO-1's UI toolkit #

Nov 10, 2026 · @Jefferson

**Status: proposed (2026-11-10).** Phase U0 (below) settles the API and the open questions, as OOLIB's plan did.
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

1. Name: OOUI (proposed) or OOTK?
2. Linked into each app, or a shared banked block or RSX from the start?
3. Which theme look: Turbo Vision blue, a grey Windows 3.1 feel, or an OO-1 house style?
4. Does OO-Amp's visualizer stay app-drawn on a canvas, or become an OOUI widget?
5. Is the bitmap backend wanted at all, or is a well-themed text UI enough for OO-1?
6. Build the drawing assist only if the bitmap backend is built?

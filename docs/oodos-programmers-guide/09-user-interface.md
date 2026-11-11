# 9 User interface (OOUI)

**Proposed.** Nothing in this chapter is built yet. It is a placeholder for OOUI, the OO-1's UI toolkit, and is
written in OOUI's phase U5, once phase U0 has settled the API. Until then the design record is
[hw/ooui.md](../../hw/ooui.md), and a program builds its own screens with OOLIB ([chapter 8](08-game-services.md)).

OOUI is a library of 6502 code that a program links in, as it links OOLIB's graphics library, to get the same
windows, menus, dialogs, lists, scroll bars and file picker as every other OO-1 program. The program describes its
widgets in a table in its own memory; OOUI draws them on the video card's layers, takes the keyboard and the mouse
from OOLIB's event area, and hands the program back events: a button clicked, a value changed, a menu item chosen.
The program never draws a widget itself.

## What the chapter will cover

- **Linking OOUI.** The archive, its `ui` calls and include file, and what it needs of OOLIB and OODOS: the event
  area, `gx` drawing, assets and video memory allocated through OOLIB, and OODOS's directory functions for the file
  picker.
- **Widget records.** Each widget's record (its type, parent, position, size, flags, text and value), handles as
  indexes into the program's table, and screens laid out as data with ca65 macros.
- **Events and the loop.** `ui_open` and `ui_poll`; focus, the keys (Tab, Enter, Space, Alt, Escape) and the mouse;
  the events a program gets back; redrawing only what changed.
- **The widgets,** in the order OOUI builds them: desktop, window, panel and label; button, checkbox, radio group
  and menus; scroll bar, list box, message box and confirm dialog; text field, bars and sliders; the file picker;
  the canvas, for a program's own pixels.
- **Themes.** The colours and frames every program shares.
- **The text backend:** 80×30 cells on layers 0 and 1, the font's box-drawing and redefined glyphs, menus and
  dialogs on layer 1 over windows on layer 0, sprites for the pointer and for sliders. A bitmap backend may follow,
  with the same API.
- **What OOUI leaves alone:** the console's video memory and Apple II mode. OOUI runs only under OODOS, in native
  mode, and needs no interrupts of its own.

OOUI allocates its screens and font through OOLIB, so it works on either revision of the video card
([chapter 8, "Video memory on revision 2"](08-game-services.md#video-memory-on-revision-2)). Its open questions,
the name among them, are in [hw/ooui.md, "Open Questions"](../../hw/ooui.md#open-questions).
